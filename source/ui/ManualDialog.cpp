#include "ManualDialog.hpp"
#include "ThemeManager.hpp"
#include "locale/GerenciadorTraducao.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace omniframe::ui {

namespace {

using locale::GerenciadorTraducao;

QString formatRichInlineText(const QString& plainLine, const QString& accentColor) {
    QString escaped = plainLine.toHtmlEscaped();

    // Destaca prefixo antes de ":" em itens de lista ou definições curtas
    const qsizetype colonIdx = escaped.indexOf(QLatin1Char(':'));
    if (colonIdx > 0 && colonIdx <= 96 && !escaped.contains(QStringLiteral("http"))) {
        const QString prefix = escaped.left(colonIdx + 1);
        const QString rest = escaped.mid(colonIdx + 1);
        return QStringLiteral("<b>%1</b>%2").arg(prefix, rest);
    }

    // Destaca prefixo antes de " — " em tabela de atalhos
    const qsizetype dashIdx = escaped.indexOf(QStringLiteral(" — "));
    if (dashIdx > 0 && dashIdx <= 56) {
        const QString keyPart = escaped.left(dashIdx);
        const QString descPart = escaped.mid(dashIdx);
        return QStringLiteral("<span style='color:%1;font-weight:600;'>%2</span>%3")
            .arg(accentColor, keyPart, descPart);
    }

    return escaped;
}

QString manualAssetRelativePath() {
    const bool isEnglish =
        (GerenciadorTraducao::instance().idiomaAtual() == locale::Idioma::InglesEstadosUnidos);
    return isEnglish
               ? QStringLiteral("MANUAL/MANUAL_en_US.txt")
               : QStringLiteral("MANUAL/MANUAL_pt_BR.txt");
}

} // namespace

ManualDialog::ManualDialog(QWidget* parent)
    : QDialog(parent) {
    setupUi();
    applyTheme();
    retranslateUi();
    loadManual();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](UiTheme) {
        applyTheme();
        loadManual();
    });
    connect(&ThemeManager::instance(), &ThemeManager::uiStyleModeChanged, this, [this](UiStyleMode) {
        applyTheme();
        loadManual();
    });
}

void ManualDialog::setupUi() {
    setModal(false);
    setWindowModality(Qt::NonModal);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
    setWindowIcon(GerenciadorTraducao::appIcon());
    resize(1160, 760);
    setMinimumSize(860, 560);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 14);
    mainLayout->setSpacing(10);

    // Cabeçalho com ícone e título em largura total (sem truncamento)
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    auto* iconLabel = new QLabel(this);
    const QPixmap iconPixmap = GerenciadorTraducao::appIcon().pixmap(40, 40);
    if (!iconPixmap.isNull()) {
        iconLabel->setPixmap(iconPixmap);
    }
    iconLabel->setFixedSize(42, 42);
    headerLayout->addWidget(iconLabel);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("ManualTitle"));
    m_subtitleLabel = new QLabel(this);
    m_subtitleLabel->setObjectName(QStringLiteral("ManualSubtitle"));
    m_subtitleLabel->setWordWrap(true);
    titleCol->addWidget(m_titleLabel);
    titleCol->addWidget(m_subtitleLabel);
    headerLayout->addLayout(titleCol, 1);

    mainLayout->addLayout(headerLayout);

    // Barra dedicada de pesquisa rápida no manual
    auto* searchLayout = new QHBoxLayout();
    searchLayout->setSpacing(6);
    m_searchInput = new QLineEdit(this);
    m_searchInput->setClearButtonEnabled(true);
    m_btnPrev = new QPushButton(this);
    m_btnNext = new QPushButton(this);
    m_btnJumpCli = new QPushButton(this);
    m_searchStatusLabel = new QLabel(this);
    m_searchStatusLabel->setObjectName(QStringLiteral("SearchStatus"));
    searchLayout->addWidget(m_searchInput, 1);
    searchLayout->addWidget(m_btnPrev);
    searchLayout->addWidget(m_btnNext);
    searchLayout->addWidget(m_btnJumpCli);
    searchLayout->addWidget(m_searchStatusLabel);
    mainLayout->addLayout(searchLayout);

    // Divisor principal: Índice de capítulos à esquerda + Leitor HTML à direita
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    auto* leftPane = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    auto* indexHeaderRow = new QHBoxLayout();
    m_indexHeaderLabel = new QLabel(leftPane);
    m_indexHeaderLabel->setObjectName(QStringLiteral("IndexHeader"));
    m_btnToggleTree = new QPushButton(leftPane);
    indexHeaderRow->addWidget(m_indexHeaderLabel, 1);
    indexHeaderRow->addWidget(m_btnToggleTree);
    leftLayout->addLayout(indexHeaderRow);

    m_indexTree = new QTreeWidget(leftPane);
    m_indexTree->setHeaderHidden(true);
    m_indexTree->setIndentation(14);
    m_indexTree->setWordWrap(true);
    m_indexTree->setAlternatingRowColors(false);
    m_indexTree->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_indexTree, 1);

    m_manualViewer = new QTextBrowser(splitter);
    m_manualViewer->setOpenLinks(true);
    m_manualViewer->setOpenExternalLinks(false);

    splitter->addWidget(leftPane);
    splitter->addWidget(m_manualViewer);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({350, 810});

    mainLayout->addWidget(splitter, 1);

    // Rodapé com botão de fechar
    auto* footerLayout = new QHBoxLayout();
    footerLayout->addStretch(1);
    m_btnClose = new QPushButton(this);
    m_btnClose->setObjectName(QStringLiteral("CloseButton"));
    m_btnClose->setMinimumWidth(110);
    footerLayout->addWidget(m_btnClose);
    mainLayout->addLayout(footerLayout);

    // Conexões e atalho Ctrl+F
    connect(m_indexTree, &QTreeWidget::itemClicked, this, &ManualDialog::onIndexItemClicked);
    connect(m_searchInput, &QLineEdit::returnPressed, this, &ManualDialog::onFindNext);
    connect(m_btnNext, &QPushButton::clicked, this, &ManualDialog::onFindNext);
    connect(m_btnPrev, &QPushButton::clicked, this, &ManualDialog::onFindPrevious);
    connect(m_btnJumpCli, &QPushButton::clicked, this, &ManualDialog::onJumpToCliSection);
    connect(m_btnToggleTree, &QPushButton::clicked, this, &ManualDialog::onToggleExpandTree);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::close);

    auto* findShortcut = new QShortcut(QKeySequence(QKeySequence::Find), this);
    connect(findShortcut, &QShortcut::activated, this, [this]() {
        m_searchInput->setFocus();
        m_searchInput->selectAll();
    });
}

void ManualDialog::applyTheme() {
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
        if (m_titleLabel) {
            QFont titleFont = m_titleLabel->font();
            titleFont.setPointSize(14);
            titleFont.setBold(true);
            m_titleLabel->setFont(titleFont);
        }
        if (m_indexHeaderLabel) {
            QFont indexHeaderFont = m_indexHeaderLabel->font();
            indexHeaderFont.setBold(true);
            m_indexHeaderLabel->setFont(indexHeaderFont);
        }
        setPalette(QPalette());
        setAttribute(Qt::WA_SetPalette, false);
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

    if (m_titleLabel) {
        m_titleLabel->setFont(QFont());
        m_titleLabel->setAttribute(Qt::WA_SetFont, false);
    }
    if (m_indexHeaderLabel) {
        m_indexHeaderLabel->setFont(QFont());
        m_indexHeaderLabel->setAttribute(Qt::WA_SetFont, false);
    }

    if (ThemeManager::instance().isLight()) {
        setStyleSheet(QStringLiteral(
            "QDialog { background-color: #eef2f7; color: #0f172a; }"
            "QLabel#ManualTitle { font-size: 15pt; font-weight: 700; color: #0f172a; }"
            "QLabel#ManualSubtitle { font-size: 9.5pt; color: #475569; }"
            "QLabel#IndexHeader { font-size: 10pt; font-weight: 700; color: #1d4ed8; padding: 4px 2px; }"
            "QLabel#SearchStatus { font-size: 8.5pt; color: #475569; min-width: 90px; }"
            "QTreeWidget { background-color: #ffffff; color: #1e293b; border: 1px solid #cbd5e1; "
            "              border-radius: 6px; padding: 4px; font-size: 9pt; }"
            "QTreeWidget::item { padding: 3px 4px; border-radius: 4px; }"
            "QTreeWidget::item:hover { background-color: #eff6ff; }"
            "QTreeWidget::item:selected { background-color: #2563eb; color: #ffffff; }"
            "QTextBrowser { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; "
            "               border-radius: 6px; padding: 14px; font-size: 10pt; "
            "               selection-background-color: #2563eb; selection-color: #ffffff; }"
            "QLineEdit { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; "
            "            border-radius: 5px; padding: 6px 10px; min-width: 220px; }"
            "QLineEdit:focus { border-color: #2563eb; }"
            "QPushButton { background-color: #ffffff; color: #0f172a; border: 1px solid #cbd5e1; "
            "              border-radius: 5px; padding: 6px 12px; font-weight: 600; }"
            "QPushButton:hover { background-color: #e2e8f0; border-color: #94a3b8; }"
            "QPushButton:pressed { background-color: #2563eb; border-color: #1d4ed8; color: #ffffff; }"
            "QPushButton#CloseButton { background-color: #2563eb; border-color: #1d4ed8; color: #ffffff; }"
            "QPushButton#CloseButton:hover { background-color: #1d4ed8; }"
            "QSplitter::handle { background-color: #cbd5e1; width: 4px; border-radius: 2px; }"));
    } else {
        setStyleSheet(QStringLiteral(
            "QDialog { background-color: #161a23; color: #e2e8f0; }"
            "QLabel#ManualTitle { font-size: 15pt; font-weight: 700; color: #f8fafc; }"
            "QLabel#ManualSubtitle { font-size: 9.5pt; color: #94a3b8; }"
            "QLabel#IndexHeader { font-size: 10pt; font-weight: 700; color: #79c0ff; padding: 4px 2px; }"
            "QLabel#SearchStatus { font-size: 8.5pt; color: #94a3b8; min-width: 90px; }"
            "QTreeWidget { background-color: #11151d; color: #dbe5f1; border: 1px solid #2a3142; "
            "              border-radius: 6px; padding: 4px; font-size: 9pt; }"
            "QTreeWidget::item { padding: 3px 4px; border-radius: 4px; }"
            "QTreeWidget::item:hover { background-color: #1c2434; }"
            "QTreeWidget::item:selected { background-color: #1f6feb; color: #ffffff; }"
            "QTextBrowser { background-color: #11151d; color: #e2e8f0; border: 1px solid #2a3142; "
            "               border-radius: 6px; padding: 14px; font-size: 10pt; "
            "               selection-background-color: #1f6feb; selection-color: #ffffff; }"
            "QLineEdit { background-color: #0e1219; color: #e2e8f0; border: 1px solid #2e3649; "
            "            border-radius: 5px; padding: 6px 10px; min-width: 220px; }"
            "QLineEdit:focus { border-color: #1f6feb; }"
            "QPushButton { background-color: #212838; color: #e2e8f0; border: 1px solid #323c52; "
            "              border-radius: 5px; padding: 6px 12px; font-weight: 600; }"
            "QPushButton:hover { background-color: #2b3448; border-color: #4b5c80; }"
            "QPushButton:pressed { background-color: #1f6feb; border-color: #388bfd; color: #ffffff; }"
            "QPushButton#CloseButton { background-color: #1f6feb; border-color: #388bfd; color: #ffffff; }"
            "QPushButton#CloseButton:hover { background-color: #2c7bf5; }"
            "QSplitter::handle { background-color: #262e3e; width: 4px; border-radius: 2px; }"));
    }
    ThemeManager::instance().applyWindowChrome(this);
}

void ManualDialog::retranslateUi() {
    setWindowTitle(GerenciadorTraducao::trText("Manual de Operação e Engenharia — OmniFrame 3D"));
    m_titleLabel->setText(GerenciadorTraducao::trText("Manual Completo de Operação — OmniFrame 3D"));
    m_subtitleLabel->setText(GerenciadorTraducao::trText("Instruções detalhadas de modelagem 3D, análise FEM, CLI (CMD/PowerShell), interface modeless e configurações."));
    m_indexHeaderLabel->setText(GerenciadorTraducao::trText("Capítulos e Subcapítulos"));
    m_searchInput->setPlaceholderText(GerenciadorTraducao::trText("Pesquisar no manual (Ctrl+F)..."));
    m_btnPrev->setText(GerenciadorTraducao::trText("Anterior"));
    m_btnNext->setText(GerenciadorTraducao::trText("Próxima"));
    m_btnJumpCli->setText(GerenciadorTraducao::trText("Comandos CLI (CMD/PowerShell)"));
    m_btnJumpCli->setToolTip(GerenciadorTraducao::trText("Ir diretamente ao capítulo completo de Linha de Comando (omniframe-cli.exe) no Windows 10/11."));
    m_btnToggleTree->setText(
        m_treeExpanded ? GerenciadorTraducao::trText("Recolher")
                       : GerenciadorTraducao::trText("Expandir"));
    m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));
}

void ManualDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    const QEvent::Type t = event ? event->type() : QEvent::None;
    if (t == QEvent::LanguageChange) {
        retranslateUi();
        loadManual();
    }
}

void ManualDialog::loadManual() {
    QString activeAnchor;
    if (QTreeWidgetItem* current = m_indexTree->currentItem()) {
        activeAnchor = current->data(0, Qt::UserRole).toString();
    }

    m_indexTree->clear();
    m_searchStatusLabel->clear();

    const QString rawText = GerenciadorTraducao::readAssetDocument(manualAssetRelativePath());
    const QString html = buildManualHtml(rawText, m_indexTree);
    m_manualViewer->setHtml(html);

    if (m_treeExpanded) {
        m_indexTree->expandAll();
    } else {
        m_indexTree->collapseAll();
    }

    if (!activeAnchor.isEmpty()) {
        for (int i = 0; i < m_indexTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* chap = m_indexTree->topLevelItem(i);
            if (chap->data(0, Qt::UserRole).toString() == activeAnchor) {
                m_indexTree->setCurrentItem(chap);
                m_manualViewer->scrollToAnchor(activeAnchor);
                return;
            }
            for (int j = 0; j < chap->childCount(); ++j) {
                QTreeWidgetItem* sub = chap->child(j);
                if (sub->data(0, Qt::UserRole).toString() == activeAnchor) {
                    m_indexTree->setCurrentItem(sub);
                    m_manualViewer->scrollToAnchor(activeAnchor);
                    return;
                }
            }
        }
    }
}

QString ManualDialog::buildManualHtml(const QString& rawText, QTreeWidget* indexTree) const {
    const QStringList lines = rawText.split(QLatin1Char('\n'));
    const bool nativeUi = ThemeManager::instance().isNativeUi();
    const bool isLight = ThemeManager::instance().isLight();

    QString inlineAccentColor = isLight ? QStringLiteral("#1d4ed8") : QStringLiteral("#79c0ff");
    QString html;
    html.reserve(rawText.size() * 2 + 4096);
    if (nativeUi) {
        const QPalette pal = m_manualViewer ? m_manualViewer->palette() : QApplication::palette();
        const QColor palBase = pal.color(QPalette::Base);
        const QColor palText = pal.color(QPalette::Text);
        const QColor palMuted = pal.color(QPalette::Disabled, QPalette::Text);
        const QColor palBorder = pal.color(QPalette::Mid);
        const QColor palLink = pal.color(QPalette::Link);

        // Em estilos nativos escuros do Qt 6 no Windows (windows11/windowsvista),
        // QPalette::AlternateBase pode permanecer claro (#f5f5f5) mesmo com Base escura.
        // Nunca aplicamos AlternateBase atrás dos blocos de texto no modo Nativo:
        // usamos fundo transparente (herdando a Base nativa do QTextBrowser) com cores
        // de texto e bordas de alto contraste garantidas para Claro e Escuro.
        const QString bodyBgCss = (!isLight && palBase.lightness() >= 120)
            ? QStringLiteral(" background-color: #1e1e1e;")
            : (isLight && palBase.lightness() < 120 ? QStringLiteral(" background-color: #ffffff;") : QString());
        const QString textColor = isLight
            ? ((palText.lightness() <= 85) ? palText.name() : QStringLiteral("#0f172a"))
            : ((palText.lightness() >= 180) ? palText.name() : QStringLiteral("#e2e8f0"));
        const QString mutedColor = isLight
            ? ((palMuted.lightness() >= 45 && palMuted.lightness() <= 130) ? palMuted.name() : QStringLiteral("#475569"))
            : ((palMuted.lightness() >= 120 && palMuted.lightness() <= 210) ? palMuted.name() : QStringLiteral("#94a3b8"));
        const QString borderColor = isLight
            ? ((palBorder.lightness() >= 150 && palBorder.lightness() <= 220) ? palBorder.name() : QStringLiteral("#cbd5e1"))
            : ((palBorder.lightness() >= 45 && palBorder.lightness() <= 115) ? palBorder.name() : QStringLiteral("#3e4452"));
        const QString linkColor = isLight
            ? ((palLink.lightness() <= 130) ? palLink.name() : QStringLiteral("#1d4ed8"))
            : ((palLink.lightness() >= 145) ? palLink.name() : QStringLiteral("#58a6ff"));
        const QString bannerTitleColor = isLight ? QStringLiteral("#0f172a") : QStringLiteral("#f8fafc");
        const QString warnTextColor    = isLight ? QStringLiteral("#92400e") : QStringLiteral("#fde68a");
        const QString warnBorderColor  = isLight ? QStringLiteral("#d97706") : QStringLiteral("#f59e0b");
        const QString noteTextColor    = isLight ? QStringLiteral("#075985") : QStringLiteral("#bae6fd");
        const QString noteBorderColor  = isLight ? QStringLiteral("#0284c7") : QStringLiteral("#38bdf8");
        const QString tipTextColor     = isLight ? QStringLiteral("#065f46") : QStringLiteral("#bbf7d0");
        const QString tipBorderColor   = isLight ? QStringLiteral("#059669") : QStringLiteral("#34d399");
        const QString fixedFontFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family().toHtmlEscaped();
        inlineAccentColor = linkColor;

        html += QStringLiteral(
                    "<html><head><style>"
                    "body { color: %1;%2 margin: 4px 10px; }"
                    ".banner { background-color: transparent; border: 1px solid %3; border-left: 4px solid %4; "
                    "          border-radius: 4px; padding: 10px 14px; margin-bottom: 14px; }"
                    ".banner-title { font-size: 12pt; font-weight: 700; color: %5; margin-bottom: 3px; }"
                    ".banner-meta { font-size: 9pt; color: %6; margin: 2px 0; }"
                    "h1 { font-size: 11.5pt; font-weight: 700; color: %4; margin-top: 16px; margin-bottom: 6px; "
                    "     padding-bottom: 4px; border-bottom: 1px solid %3; }"
                    "h2 { font-size: 10.5pt; font-weight: 700; color: %4; margin-top: 12px; margin-bottom: 4px; }"
                    "p { margin: 4px 0 8px 0; font-size: 9.8pt; line-height: 145%; color: %1; }"
                    "ul, ol { margin: 4px 0 8px 20px; padding-left: 8px; }"
                    "li { margin: 3px 0; font-size: 9.8pt; line-height: 142%; color: %1; }")
                    .arg(textColor, bodyBgCss, borderColor, linkColor, bannerTitleColor, mutedColor);
        html += QStringLiteral(
                    ".callout-warn { background-color: transparent; border: 1px solid %1; border-left: 4px solid %2; "
                    "                padding: 7px 11px; margin: 7px 0; border-radius: 4px; color: %3; "
                    "                font-size: 9.6pt; line-height: 142%; }"
                    ".callout-note { background-color: transparent; border: 1px solid %1; border-left: 4px solid %4; "
                    "                padding: 7px 11px; margin: 7px 0; border-radius: 4px; color: %5; "
                    "                font-size: 9.6pt; line-height: 142%; }"
                    ".callout-tip  { background-color: transparent; border: 1px solid %1; border-left: 4px solid %6; "
                    "                padding: 7px 11px; margin: 7px 0; border-radius: 4px; color: %7; "
                    "                font-size: 9.6pt; line-height: 142%; }")
                    .arg(borderColor, warnBorderColor, warnTextColor, noteBorderColor, noteTextColor,
                         tipBorderColor, tipTextColor);
        html += QStringLiteral(
                    ".cli-cmd      { background-color: transparent; border: 1px solid %1; border-left: 3px solid %2; "
                    "                padding: 5px 10px; margin: 4px 0; border-radius: 4px; color: %3; "
                    "                font-family: '%4', monospace; font-size: 9.2pt; }"
                    "</style></head><body>")
                    .arg(borderColor, linkColor, textColor, fixedFontFamily);
    } else if (isLight) {
        html += QStringLiteral(
            "<html><head><style>"
            "body { font-family: 'Segoe UI', Arial, sans-serif; color: #1e293b; margin: 4px 10px; }"
            ".banner { background-color: #eff6ff; border: 1px solid #bfdbfe; border-left: 4px solid #2563eb; "
            "          border-radius: 6px; padding: 10px 14px; margin-bottom: 14px; }"
            ".banner-title { font-size: 12pt; font-weight: 700; color: #0f172a; margin-bottom: 3px; }"
            ".banner-meta { font-size: 9pt; color: #475569; margin: 2px 0; }"
            "h1 { font-size: 11.5pt; font-weight: 700; color: #1d4ed8; margin-top: 16px; margin-bottom: 6px; "
            "     padding-bottom: 4px; border-bottom: 1px solid #cbd5e1; }"
            "h2 { font-size: 10.5pt; font-weight: 700; color: #0369a1; margin-top: 12px; margin-bottom: 4px; }"
            "p { margin: 4px 0 8px 0; font-size: 9.8pt; line-height: 145%; }"
            "ul, ol { margin: 4px 0 8px 20px; padding-left: 8px; }"
            "li { margin: 3px 0; font-size: 9.8pt; line-height: 142%; }"
            ".callout-warn { background-color: #fffbeb; border-left: 4px solid #d97706; padding: 7px 11px; "
            "                margin: 7px 0; border-radius: 4px; color: #92400e; font-size: 9.6pt; line-height: 142%; }"
            ".callout-note { background-color: #eff6ff; border-left: 4px solid #0284c7; padding: 7px 11px; "
            "                margin: 7px 0; border-radius: 4px; color: #075985; font-size: 9.6pt; line-height: 142%; }"
            ".callout-tip  { background-color: #ecfdf5; border-left: 4px solid #059669; padding: 7px 11px; "
            "                margin: 7px 0; border-radius: 4px; color: #065f46; font-size: 9.6pt; line-height: 142%; }"
            ".cli-cmd      { background-color: #f1f5f9; border: 1px solid #cbd5e1; border-left: 3px solid #2563eb; "
            "                padding: 5px 10px; margin: 4px 0; border-radius: 4px; color: #0f172a; "
            "                font-family: 'Cascadia Code', 'Consolas', monospace; font-size: 9.2pt; }"
            "</style></head><body>");
    } else {
        html += QStringLiteral(
            "<html><head><style>"
            "body { font-family: 'Segoe UI', Arial, sans-serif; color: #dbe4f0; margin: 4px 10px; }"
            ".banner { background-color: #161d2b; border: 1px solid #2d4263; border-left: 4px solid #38bdf8; "
            "          border-radius: 6px; padding: 10px 14px; margin-bottom: 14px; }"
            ".banner-title { font-size: 12pt; font-weight: 700; color: #f8fafc; margin-bottom: 3px; }"
            ".banner-meta { font-size: 9pt; color: #94a3b8; margin: 2px 0; }"
            "h1 { font-size: 11.5pt; font-weight: 700; color: #79c0ff; margin-top: 16px; margin-bottom: 6px; "
            "     padding-bottom: 4px; border-bottom: 1px solid #2b3549; }"
            "h2 { font-size: 10.5pt; font-weight: 700; color: #38bdf8; margin-top: 12px; margin-bottom: 4px; }"
            "p { margin: 4px 0 8px 0; font-size: 9.8pt; line-height: 145%; }"
            "ul, ol { margin: 4px 0 8px 20px; padding-left: 8px; }"
            "li { margin: 3px 0; font-size: 9.8pt; line-height: 142%; }"
            ".callout-warn { background-color: #2b1d1a; border-left: 4px solid #f59e0b; padding: 7px 11px; "
            "                margin: 7px 0; border-radius: 4px; color: #fde68a; font-size: 9.6pt; line-height: 142%; }"
            ".callout-note { background-color: #162436; border-left: 4px solid #38bdf8; padding: 7px 11px; "
            "                margin: 7px 0; border-radius: 4px; color: #bae6fd; font-size: 9.6pt; line-height: 142%; }"
            ".callout-tip  { background-color: #162922; border-left: 4px solid #34d399; padding: 7px 11px; "
            "                margin: 7px 0; border-radius: 4px; color: #bbf7d0; font-size: 9.6pt; line-height: 142%; }"
            ".cli-cmd      { background-color: #0b0f17; border: 1px solid #263145; border-left: 3px solid #38bdf8; "
            "                padding: 5px 10px; margin: 4px 0; border-radius: 4px; color: #e2e8f0; "
            "                font-family: 'Cascadia Code', 'Consolas', monospace; font-size: 9.2pt; }"
            "</style></head><body>");
    }

    static const QRegularExpression rxChapter(QStringLiteral("^(CAPÍTULO|CHAPTER)\\s+(\\d+)\\s+[—-]\\s+(.+)$"));
    static const QRegularExpression rxSubchapter(QStringLiteral("^(\\d+\\.\\d+)\\s+(.+)$"));
    static const QRegularExpression rxOrdered(QStringLiteral("^(\\d+)\\.\\s+(.+)$"));

    QTreeWidgetItem* currentChapterItem = nullptr;
    bool inBanner = false;
    bool bannerOpen = false;
    bool inUnorderedList = false;
    bool inOrderedList = false;

    auto closeLists = [&]() {
        if (inUnorderedList) {
            html += QStringLiteral("</ul>");
            inUnorderedList = false;
        }
        if (inOrderedList) {
            html += QStringLiteral("</ol>");
            inOrderedList = false;
        }
    };

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();

        if (line.startsWith(QStringLiteral("===="))) {
            closeLists();
            if (!inBanner) {
                inBanner = true;
                bannerOpen = true;
                html += QStringLiteral("<div class='banner'>");
            } else {
                inBanner = false;
                if (bannerOpen) {
                    html += QStringLiteral("</div>");
                    bannerOpen = false;
                }
            }
            continue;
        }

        if (bannerOpen) {
            if (!line.isEmpty()) {
                if (line.startsWith(QStringLiteral("OMNIFRAME 3D"))) {
                    html += QStringLiteral("<div class='banner-title'>%1</div>").arg(line.toHtmlEscaped());
                } else {
                    html += QStringLiteral("<div class='banner-meta'>%1</div>").arg(line.toHtmlEscaped());
                }
            }
            continue;
        }

        if (line.isEmpty()) {
            closeLists();
            continue;
        }

        const QRegularExpressionMatch chapMatch = rxChapter.match(line);
        if (chapMatch.hasMatch()) {
            closeLists();
            const QString anchor = QStringLiteral("cap-%1").arg(chapMatch.captured(2));
            html += QStringLiteral("<h1 id='%1'><a name='%1'></a>%2</h1>")
                        .arg(anchor, line.toHtmlEscaped());

            currentChapterItem = new QTreeWidgetItem(indexTree);
            currentChapterItem->setText(0, line);
            currentChapterItem->setToolTip(0, line);
            currentChapterItem->setData(0, Qt::UserRole, anchor);
            QFont boldFont = currentChapterItem->font(0);
            boldFont.setBold(true);
            currentChapterItem->setFont(0, boldFont);
            continue;
        }

        const QRegularExpressionMatch subMatch = rxSubchapter.match(line);
        if (subMatch.hasMatch()) {
            closeLists();
            QString cleanNum = subMatch.captured(1);
            cleanNum.replace(QLatin1Char('.'), QLatin1Char('-'));
            const QString anchor = QStringLiteral("sub-%1").arg(cleanNum);
            html += QStringLiteral("<h2 id='%1'><a name='%1'></a>%2</h2>")
                        .arg(anchor, line.toHtmlEscaped());

            auto* parentItem = currentChapterItem ? currentChapterItem : indexTree->invisibleRootItem();
            auto* subItem = new QTreeWidgetItem(parentItem);
            subItem->setText(0, line);
            subItem->setToolTip(0, line);
            subItem->setData(0, Qt::UserRole, anchor);
            continue;
        }

        if (line.startsWith(QStringLiteral("• "))) {
            if (inOrderedList) {
                html += QStringLiteral("</ol>");
                inOrderedList = false;
            }
            if (!inUnorderedList) {
                html += QStringLiteral("<ul>");
                inUnorderedList = true;
            }
            html += QStringLiteral("<li>%1</li>").arg(formatRichInlineText(line.mid(2), inlineAccentColor));
            continue;
        }

        const QRegularExpressionMatch ordMatch = rxOrdered.match(line);
        if (ordMatch.hasMatch()) {
            if (inUnorderedList) {
                html += QStringLiteral("</ul>");
                inUnorderedList = false;
            }
            if (!inOrderedList) {
                html += QStringLiteral("<ol>");
                inOrderedList = true;
            }
            html += QStringLiteral("<li>%1</li>").arg(formatRichInlineText(ordMatch.captured(2), inlineAccentColor));
            continue;
        }

        closeLists();

        if (line.startsWith(QStringLiteral("ATENÇÃO:")) || line.startsWith(QStringLiteral("WARNING:"))) {
            html += QStringLiteral("<div class='callout-warn'>%1</div>").arg(formatRichInlineText(line, inlineAccentColor));
        } else if (line.startsWith(QStringLiteral("NOTA:")) || line.startsWith(QStringLiteral("NOTE:"))) {
            html += QStringLiteral("<div class='callout-note'>%1</div>").arg(formatRichInlineText(line, inlineAccentColor));
        } else if (line.startsWith(QStringLiteral("DICA:")) || line.startsWith(QStringLiteral("TIP:"))) {
            html += QStringLiteral("<div class='callout-tip'>%1</div>").arg(formatRichInlineText(line, inlineAccentColor));
        } else if (line.startsWith(QStringLiteral("CMD> ")) ||
                   line.startsWith(QStringLiteral("PS> ")) ||
                   line.startsWith(QStringLiteral("omniframe-cli ")) ||
                   line.startsWith(QStringLiteral("OmniFrame.exe "))) {
            html += QStringLiteral("<div class='cli-cmd'>%1</div>").arg(line.toHtmlEscaped());
        } else {
            html += QStringLiteral("<p>%1</p>").arg(line.toHtmlEscaped());
        }
    }

    closeLists();
    html += QStringLiteral("</body></html>");
    return html;
}

void ManualDialog::onIndexItemClicked(QTreeWidgetItem* item, int column) {
    (void)column;
    if (!item) return;

    const QString anchor = item->data(0, Qt::UserRole).toString();
    if (!anchor.isEmpty()) {
        m_manualViewer->scrollToAnchor(anchor);
    }
}

void ManualDialog::onFindNext() {
    findText(false);
}

void ManualDialog::onFindPrevious() {
    findText(true);
}

void ManualDialog::findText(bool backward) {
    const QString query = m_searchInput->text().trimmed();
    if (query.isEmpty()) {
        m_searchStatusLabel->clear();
        return;
    }

    QTextDocument::FindFlags flags;
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }

    bool found = m_manualViewer->find(query, flags);
    if (!found) {
        // Faz busca circular do início ou do fim do documento
        QTextCursor cursor = m_manualViewer->textCursor();
        cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        m_manualViewer->setTextCursor(cursor);
        found = m_manualViewer->find(query, flags);
    }

    m_searchStatusLabel->setText(
        found ? GerenciadorTraducao::trText("Encontrado")
              : GerenciadorTraducao::trText("Não encontrado"));
}

void ManualDialog::onToggleExpandTree() {
    m_treeExpanded = !m_treeExpanded;
    if (m_treeExpanded) {
        m_indexTree->expandAll();
    } else {
        m_indexTree->collapseAll();
    }
    m_btnToggleTree->setText(
        m_treeExpanded ? GerenciadorTraducao::trText("Recolher")
                       : GerenciadorTraducao::trText("Expandir"));
}

void ManualDialog::onJumpToCliSection() {
    for (int i = 0; i < m_indexTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_indexTree->topLevelItem(i);
        const QString title = item->text(0);
        if (title.contains(QStringLiteral("CLI"), Qt::CaseInsensitive) ||
            title.contains(QStringLiteral("omniframe-cli"), Qt::CaseInsensitive) ||
            title.contains(QStringLiteral("Linha de Comando"), Qt::CaseInsensitive) ||
            title.contains(QStringLiteral("Command-Line"), Qt::CaseInsensitive)) {
            m_indexTree->setCurrentItem(item);
            m_indexTree->expandItem(item);
            const QString anchor = item->data(0, Qt::UserRole).toString();
            if (!anchor.isEmpty()) {
                m_manualViewer->scrollToAnchor(anchor);
            }
            return;
        }
    }
}

} // namespace omniframe::ui
