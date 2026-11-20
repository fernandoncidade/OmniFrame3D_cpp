#include "ui/GuidedSessionDialog.hpp"

#include "locale/GerenciadorTraducao.hpp"
#include "render/Viewport3D.hpp"
#include "ui/Dialogs.hpp"
#include "ui/EngineeringDialogs.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ThemeManager.hpp"
#include "ui/WindowZOrderManager.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMoveEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QRectF>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextDocument>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace omniframe {

namespace {

QWidget* firstVisibleWidgetOrFallback(QWidget* primary, QWidget* fallback) {
    if (primary && primary->isVisible()) {
        return primary;
    }
    return fallback;
}

MaterialId findMaterialIdByKeywords(const StructuralModel& model, const QStringList& keywords) {
    for (const auto& m : model.getMaterials()) {
        const QString name = QString::fromStdString(m.name);
        for (const QString& kw : keywords) {
            if (name.contains(kw, Qt::CaseInsensitive)) {
                return m.id;
            }
        }
    }
    return model.getMaterials().empty() ? 1 : model.getMaterials().front().id;
}

SectionId findSectionIdByKeywords(const StructuralModel& model, const QStringList& keywords) {
    for (const auto& s : model.getCrossSections()) {
        const QString name = QString::fromStdString(s.name);
        for (const QString& kw : keywords) {
            if (name.contains(kw, Qt::CaseInsensitive)) {
                return s.id;
            }
        }
    }
    return model.getCrossSections().empty() ? 1 : model.getCrossSections().front().id;
}

void selectListRowByKeywords(QListWidget* list, const QStringList& keywords) {
    if (!list || list->count() <= 0) return;
    int targetRow = 0;
    for (int i = 0; i < list->count(); ++i) {
        const QString txt = list->item(i)->text();
        bool matched = false;
        for (const QString& kw : keywords) {
            if (txt.contains(kw, Qt::CaseInsensitive)) {
                matched = true;
                break;
            }
        }
        if (matched) {
            targetRow = i;
            break;
        }
    }
    if (auto* sb = list->verticalScrollBar()) {
        sb->setValue(sb->maximum());
    }
    list->setCurrentRow(targetRow);
    list->scrollToItem(list->item(targetRow), QAbstractItemView::PositionAtCenter);
}

void scrollTableToBottomAndTop(QTableWidget* table) {
    if (!table) return;
    if (auto* sb = table->verticalScrollBar()) {
        if (sb->maximum() > sb->minimum()) {
            sb->setValue(sb->maximum());
        }
    }
}

void addTrussChordBars(StructuralModel& model, MaterialId matId, SectionId secId) {
    const int numBays = 4;
    const int offsetTop = (numBays + 1) * 2;
    for (int i = 0; i < numBays; ++i) {
        const int bL1 = i * 2 + 1;
        const int bR1 = i * 2 + 2;
        const int bL2 = (i + 1) * 2 + 1;
        const int bR2 = (i + 1) * 2 + 2;

        const int tL1 = offsetTop + bL1;
        const int tR1 = offsetTop + bR1;
        const int tL2 = offsetTop + bL2;
        const int tR2 = offsetTop + bR2;

        model.addElement(bL1, bL2, matId, secId);
        model.addElement(bR1, bR2, matId, secId);
        model.addElement(tL1, tL2, matId, secId);
        model.addElement(tR1, tR2, matId, secId);
    }
}

void addTrussWebAndBracingBars(StructuralModel& model, MaterialId matId, SectionId secId) {
    const int numBays = 4;
    const int offsetTop = (numBays + 1) * 2;
    for (int i = 0; i < numBays; ++i) {
        const int bL1 = i * 2 + 1;
        const int bR1 = i * 2 + 2;
        const int bL2 = (i + 1) * 2 + 1;
        const int bR2 = (i + 1) * 2 + 2;

        const int tL1 = offsetTop + bL1;
        const int tR1 = offsetTop + bR1;
        const int tL2 = offsetTop + bL2;
        const int tR2 = offsetTop + bR2;

        model.addElement(bL1, tL1, matId, secId);
        model.addElement(bR1, tR1, matId, secId);
        model.addElement(bL1, bR1, matId, secId);
        model.addElement(tL1, tR1, matId, secId);
        model.addElement(bL1, tR1, matId, secId);
        model.addElement(bL1, tL2, matId, secId);
        model.addElement(bR1, tR2, matId, secId);
        model.addElement(bL1, bR2, matId, secId);
        model.addElement(tL1, tR2, matId, secId);
    }

    const int lastBL = numBays * 2 + 1;
    const int lastBR = numBays * 2 + 2;
    const int lastTL = offsetTop + lastBL;
    const int lastTR = offsetTop + lastBR;

    model.addElement(lastBL, lastTL, matId, secId);
    model.addElement(lastBR, lastTR, matId, secId);
    model.addElement(lastBL, lastBR, matId, secId);
    model.addElement(lastTL, lastTR, matId, secId);
    model.addElement(lastBL, lastTR, matId, secId);
}

QPointF clampPointToRect(const QPointF& pt, const QRectF& rect) {
    return QPointF(std::clamp(pt.x(), rect.left(), rect.right()),
                   std::clamp(pt.y(), rect.top(), rect.bottom()));
}

} // namespace

// ============================================================================
// GuidedTargetOverlay: Spotlight + Seta + Callout acoplado a janela hospedeira
// ============================================================================

GuidedTargetOverlay::GuidedTargetOverlay(QWidget* hostWindow)
    : QWidget(nullptr) {
    setObjectName(QStringLiteral("guided_target_overlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);
    hide();
    if (hostWindow) {
        hide();
        setParent(hostWindow, Qt::Widget);
        m_hostWindow = hostWindow;
        m_hostWindow->installEventFilter(this);
    }
}

GuidedTargetOverlay::~GuidedTargetOverlay() {
    if (m_hostWindow) {
        m_hostWindow->removeEventFilter(this);
    }
}

void GuidedTargetOverlay::setShowCalloutCard(bool show) {
    if (m_showCalloutCard != show) {
        m_showCalloutCard = show;
        if (!m_showCalloutCard) {
            m_calloutRectInHost = QRect();
        }
        if (isVisible()) {
            syncGeometryFromHost();
            update();
        }
    }
}

void GuidedTargetOverlay::setShowSpotlight(bool show) {
    if (m_showSpotlight != show) {
        m_showSpotlight = show;
        if (isVisible()) {
            update();
        }
    }
}

void GuidedTargetOverlay::setHighlight(QWidget* hostWindow,
                                       QWidget* targetWidget,
                                       const QRect& targetRectInTarget,
                                       int phase,
                                       const QString& badgeText,
                                       const QString& unifiedHtmlBody) {
    if (!hostWindow || !targetWidget || !hostWindow->isVisible()) {
        clearHighlight();
        return;
    }

    if (m_hostWindow.data() != hostWindow) {
        if (m_hostWindow) {
            m_hostWindow->removeEventFilter(this);
        }
        hide();
        setParent(hostWindow, Qt::Widget);
        m_hostWindow = hostWindow;
        m_hostWindow->installEventFilter(this);
    }

    m_targetWidget = targetWidget;
    m_targetRectInTarget = targetRectInTarget;
    m_phase = phase;
    m_badgeText = badgeText;
    m_unifiedHtmlBody = unifiedHtmlBody;

    syncGeometryFromHost();
    raise();
    show();
    update();
}

void GuidedTargetOverlay::clearHighlight() {
    if (m_hostWindow) {
        m_hostWindow->removeEventFilter(this);
    }
    m_targetWidget.clear();
    m_targetRectInTarget = QRect();
    m_targetRectInHost = QRect();
    m_calloutRectInHost = QRect();
    m_phase = 0;
    hide();
    if (parentWidget() != nullptr) {
        setParent(nullptr, Qt::Widget);
    }
    m_hostWindow.clear();
}

void GuidedTargetOverlay::refreshTheme() {
    if (isVisible()) {
        update();
    }
}

void GuidedTargetOverlay::advancePulse() {
    m_pulseTick = (m_pulseTick + 1) % 8;
    if (isVisible()) {
        update();
    }
}

void GuidedTargetOverlay::syncAndUpdate() {
    if (isVisible()) {
        syncGeometryFromHost();
        update();
    }
}

QRect GuidedTargetOverlay::lastCalloutCardGlobalRect() const {
    if (!m_hostWindow || !isVisible() || !m_showCalloutCard || m_calloutRectInHost.isEmpty()) {
        return QRect();
    }
    return QRect(m_hostWindow->mapToGlobal(m_calloutRectInHost.topLeft()), m_calloutRectInHost.size());
}

void GuidedTargetOverlay::computeCalloutLayout(QRect& outCardRect,
                                               int& outHeaderH,
                                               int& outPadY,
                                               int& outFontPt,
                                               int& outInnerW,
                                               int& outBadgeH) const {
    outCardRect = QRect();
    outHeaderH = 0;
    outPadY = 8;
    outFontPt = std::max(8, font().pointSize() - 1);
    outInnerW = 280;
    outBadgeH = 18;

    if (!m_hostWindow || !m_showCalloutCard || m_unifiedHtmlBody.isEmpty()) {
        return;
    }

    const QRect avail = rect().adjusted(8, 8, -8, -8);
    if (avail.width() < 200 || avail.height() < 90) {
        return;
    }

    // Coleta os retângulos ocupados por todas as janelas filhas / top-level visíveis
    // para que o cartão Callout nunca caia sobre uma janela filha.
    std::vector<QRect> forbiddenWindows;
    for (QWidget* topWin : QApplication::topLevelWidgets()) {
        if (!topWin || !topWin->isVisible() || topWin == m_hostWindow.data()) {
            continue;
        }
        const Qt::WindowType wt = topWin->windowType();
        if (wt == Qt::Popup || wt == Qt::ToolTip || wt == Qt::SplashScreen) {
            continue;
        }
        const QRect globalWinRect = topWin->frameGeometry().united(
            QRect(topWin->mapToGlobal(QPoint(0, 0)), topWin->size()));
        const QPoint localTL = m_hostWindow->mapFromGlobal(globalWinRect.topLeft());
        const QPoint localBR = m_hostWindow->mapFromGlobal(globalWinRect.bottomRight());
        const QRect localWinRect = QRect(localTL, localBR).adjusted(-8, -8, 8, 8).intersected(rect());
        if (!localWinRect.isEmpty()) {
            forbiddenWindows.push_back(localWinRect);
        }
    }

    const QRect targetGuard = m_targetRectInHost.isValid()
        ? m_targetRectInHost.adjusted(-10, -10, 10, 10).intersected(rect())
        : QRect();

    QFont badgeFont = font();
    badgeFont.setPointSize(std::max(8, badgeFont.pointSize() - 1));
    badgeFont.setBold(true);
    const QFontMetrics fmBadge(badgeFont);

    const int basePt = std::max(8, font().pointSize() - 1);
    const int minPt = std::max(7, basePt - 1);
    struct SizeConfig {
        int cardW;
        int ptSize;
        int padY;
    };
    const std::vector<SizeConfig> configs = {
        {std::clamp(avail.width() * 38 / 100, 320, std::min(460, avail.width())), basePt, 8},
        {std::min(620, avail.width()), basePt, 7},
        {std::min(700, avail.width()), minPt, 5},
        {std::clamp(avail.width() / 3, 270, std::min(340, avail.width())), minPt, 5}
    };

    long long bestWinOverlap = std::numeric_limits<long long>::max();
    long long bestTargetOverlap = std::numeric_limits<long long>::max();
    long long bestDistSq = std::numeric_limits<long long>::max();

    const QPoint targetCenter = m_targetRectInHost.isValid() ? m_targetRectInHost.center() : avail.center();

    QTextDocument doc;
    doc.setDocumentMargin(0.0);

    for (const SizeConfig& cfg : configs) {
        const int cardW = std::clamp(cfg.cardW, 200, avail.width());
        const int innerW = std::max(120, cardW - 22);
        const QRect rBadge = fmBadge.boundingRect(QRect(0, 0, innerW, 200), Qt::TextWordWrap, m_badgeText);
        const int headerH = rBadge.height() + 10;

        QFont docFont = font();
        docFont.setPointSize(cfg.ptSize);
        doc.setDefaultFont(docFont);
        doc.setHtml(QStringLiteral("<body>%1</body>").arg(m_unifiedHtmlBody));
        doc.setTextWidth(static_cast<qreal>(innerW));

        const int docH = static_cast<int>(std::ceil(doc.size().height()));
        const int cardH = std::min(avail.height(), headerH + cfg.padY + docH + 10);

        const int minX = avail.left();
        const int maxX = std::max(avail.left(), avail.right() - cardW + 1);
        const int minY = avail.top();
        const int maxY = std::max(avail.top(), avail.bottom() - cardH + 1);

        std::vector<QPoint> candidates;
        candidates.reserve(96);
        auto addCandidate = [&](int x, int y) {
            candidates.emplace_back(std::clamp(x, minX, maxX), std::clamp(y, minY, maxY));
        };

        const int gap = 14;
        if (m_targetRectInHost.isValid()) {
            addCandidate(targetCenter.x() - cardW / 2, m_targetRectInHost.bottom() + gap);
            addCandidate(targetCenter.x() - cardW / 2, m_targetRectInHost.top() - gap - cardH);
            addCandidate(m_targetRectInHost.right() + gap, targetCenter.y() - cardH / 2);
            addCandidate(m_targetRectInHost.left() - gap - cardW, targetCenter.y() - cardH / 2);
        }

        // Cantos da área útil
        addCandidate(minX, minY);
        addCandidate(maxX, minY);
        addCandidate(minX, maxY);
        addCandidate(maxX, maxY);

        // Posições imediatamente adjacentes às bordas livres de cada janela filha
        for (const QRect& fw : forbiddenWindows) {
            addCandidate(fw.left() - cardW, minY);
            addCandidate(fw.left() - cardW, targetCenter.y() - cardH / 2);
            addCandidate(fw.left() - cardW, fw.top());
            addCandidate(fw.left() - cardW, maxY);

            addCandidate(fw.right() + 1, minY);
            addCandidate(fw.right() + 1, targetCenter.y() - cardH / 2);
            addCandidate(fw.right() + 1, maxY);

            addCandidate(minX, fw.top() - cardH);
            addCandidate(targetCenter.x() - cardW / 2, fw.top() - cardH);
            addCandidate(maxX, fw.top() - cardH);

            addCandidate(minX, fw.bottom() + 1);
            addCandidate(targetCenter.x() - cardW / 2, fw.bottom() + 1);
            addCandidate(maxX, fw.bottom() + 1);
        }

        // Varredura em grade sobre a área disponível para detectar qualquer bolso livre
        const int stepsX = 8;
        const int stepsY = 8;
        for (int ix = 0; ix <= stepsX; ++ix) {
            const int gx = minX + ((maxX - minX) * ix) / stepsX;
            for (int iy = 0; iy <= stepsY; ++iy) {
                const int gy = minY + ((maxY - minY) * iy) / stepsY;
                addCandidate(gx, gy);
            }
        }

        bool foundZeroInConfig = false;
        for (const QPoint& pt : candidates) {
            const QRect cand(pt.x(), pt.y(), cardW, cardH);
            long long winOverlap = 0;
            for (const QRect& fw : forbiddenWindows) {
                const QRect inter = cand.intersected(fw);
                if (!inter.isEmpty()) {
                    winOverlap += static_cast<long long>(inter.width()) * static_cast<long long>(inter.height());
                }
            }
            long long tgtOverlap = 0;
            if (!targetGuard.isEmpty()) {
                const QRect inter = cand.intersected(targetGuard);
                if (!inter.isEmpty()) {
                    tgtOverlap = static_cast<long long>(inter.width()) * static_cast<long long>(inter.height());
                }
            }
            const long long dx = static_cast<long long>(cand.center().x()) - static_cast<long long>(targetCenter.x());
            const long long dy = static_cast<long long>(cand.center().y()) - static_cast<long long>(targetCenter.y());
            const long long distSq = dx * dx + dy * dy;

            if (winOverlap == 0 && tgtOverlap == 0) {
                foundZeroInConfig = true;
            }

            const bool better = (winOverlap < bestWinOverlap)
                || (winOverlap == bestWinOverlap && tgtOverlap < bestTargetOverlap)
                || (winOverlap == bestWinOverlap && tgtOverlap == bestTargetOverlap && distSq < bestDistSq);

            if (better) {
                bestWinOverlap = winOverlap;
                bestTargetOverlap = tgtOverlap;
                bestDistSq = distSq;
                outCardRect = cand;
                outHeaderH = headerH;
                outPadY = cfg.padY;
                outFontPt = cfg.ptSize;
                outInnerW = innerW;
                outBadgeH = rBadge.height();
            }
        }

        if (foundZeroInConfig) {
            break;
        }
    }
}

void GuidedTargetOverlay::syncGeometryFromHost() {
    if (!m_hostWindow) {
        m_targetRectInHost = QRect();
        m_calloutRectInHost = QRect();
        return;
    }
    const QRect hostRect = m_hostWindow->rect();
    if (geometry() != hostRect) {
        setGeometry(hostRect);
    }

    if (!m_targetWidget) {
        m_targetRectInHost = hostRect.adjusted(24, 24, -24, -24);
    } else {
        const QRect srcRect = m_targetRectInTarget.isValid() ? m_targetRectInTarget : m_targetWidget->rect();
        if (srcRect.isEmpty()) {
            m_targetRectInHost = QRect();
        } else {
            QPoint topLeft = srcRect.topLeft();
            if (m_targetWidget.data() != m_hostWindow.data() && m_targetWidget->window() == m_hostWindow.data()) {
                topLeft = m_targetWidget->mapTo(m_hostWindow.data(), srcRect.topLeft());
            } else if (m_targetWidget->window() != m_hostWindow.data()) {
                topLeft = m_hostWindow->mapFromGlobal(m_targetWidget->mapToGlobal(srcRect.topLeft()));
            }

            QRect mapped(topLeft, srcRect.size());
            const QRect safeHost = hostRect.adjusted(3, 3, -3, -3);
            mapped = mapped.intersected(safeHost);
            if (mapped.isEmpty()) {
                mapped = QRect(hostRect.center().x() - 40, hostRect.center().y() - 18, 80, 36);
            }
            m_targetRectInHost = mapped;
        }
    }

    if (m_showCalloutCard) {
        int hdrH = 0, padY = 0, fontPt = 0, innerW = 0, badgeH = 0;
        computeCalloutLayout(m_calloutRectInHost, hdrH, padY, fontPt, innerW, badgeH);
    } else {
        m_calloutRectInHost = QRect();
    }
}

bool GuidedTargetOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_hostWindow.data() && event) {
        const QEvent::Type t = event->type();
        if (t == QEvent::Resize || t == QEvent::LayoutRequest || t == QEvent::Show) {
            if (isVisible()) {
                syncGeometryFromHost();
                raise();
                update();
            }
        } else if (t == QEvent::PaletteChange || t == QEvent::StyleChange) {
            refreshTheme();
        } else if (t == QEvent::Hide || t == QEvent::Close) {
            clearHighlight();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void GuidedTargetOverlay::paintEvent(QPaintEvent* /*event*/) {
    if (!isVisible() || m_targetRectInHost.isEmpty() || !m_hostWindow) {
        return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool light = ThemeManager::instance().isLight();
    const bool isPhase2 = (m_phase == 2);

    const QColor accent = isPhase2
        ? (light ? QColor(QStringLiteral("#059669")) : QColor(QStringLiteral("#34D399")))
        : (light ? QColor(QStringLiteral("#D97706")) : QColor(QStringLiteral("#FBBF24")));
    const QColor accentStrong = isPhase2
        ? (light ? QColor(QStringLiteral("#047857")) : QColor(QStringLiteral("#10B981")))
        : (light ? QColor(QStringLiteral("#B45309")) : QColor(QStringLiteral("#F59E0B")));

    const double pulse = 0.5 + 0.5 * std::sin(m_pulseTick * 0.7853981633974483);

    const qreal haloExpand = 5.0 + 3.0 * pulse;
    const QRectF focusRect = QRectF(m_targetRectInHost).adjusted(-2.5, -2.5, 2.5, 2.5);
    const QRectF haloRect = QRectF(m_targetRectInHost).adjusted(-haloExpand, -haloExpand, haloExpand, haloExpand);

    if (m_showSpotlight) {
        // 1. Halo externo pulsante ao redor do widget/controle em foco
        QColor haloFill = accent;
        haloFill.setAlpha(static_cast<int>(light ? (22 + 18 * pulse) : (28 + 24 * pulse)));
        QColor haloPenColor = accent;
        haloPenColor.setAlpha(static_cast<int>(90 + 85 * pulse));
        p.setBrush(haloFill);
        p.setPen(QPen(haloPenColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawRoundedRect(haloRect, 7.0, 7.0);

        // 2. Quadro nítido de realce
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accentStrong, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawRoundedRect(focusRect, 5.0, 5.0);

        // 3. Cantoneiras de mira (4 L nos cantos) para leitura visual imediata
        const qreal cLen = std::min<qreal>(11.0, std::max<qreal>(6.0, std::min(focusRect.width(), focusRect.height()) * 0.35));
        const QRectF b = focusRect.adjusted(-2.0, -2.0, 2.0, 2.0);
        p.setPen(QPen(accent, 3.0, Qt::SolidLine, Qt::RoundCap));
        // Topo-Esquerda
        p.drawLine(QPointF(b.left(), b.top() + cLen), QPointF(b.left(), b.top()));
        p.drawLine(QPointF(b.left(), b.top()), QPointF(b.left() + cLen, b.top()));
        // Topo-Direita
        p.drawLine(QPointF(b.right() - cLen, b.top()), QPointF(b.right(), b.top()));
        p.drawLine(QPointF(b.right(), b.top()), QPointF(b.right(), b.top() + cLen));
        // Base-Esquerda
        p.drawLine(QPointF(b.left(), b.bottom() - cLen), QPointF(b.left(), b.bottom()));
        p.drawLine(QPointF(b.left(), b.bottom()), QPointF(b.left() + cLen, b.bottom()));
        // Base-Direita
        p.drawLine(QPointF(b.right() - cLen, b.bottom()), QPointF(b.right(), b.bottom()));
        p.drawLine(QPointF(b.right(), b.bottom()), QPointF(b.right(), b.bottom() - cLen));
    }

    if (!m_showCalloutCard) {
        return;
    }

    // 4. Cartão Callout Visual moderno unificado posicionado sem sobrepor janelas filhas
    QRect cardRect;
    int headerH = 0;
    int padY = 8;
    int fontPt = std::max(8, font().pointSize() - 1);
    int innerW = 280;
    int badgeH = 18;
    computeCalloutLayout(cardRect, headerH, padY, fontPt, innerW, badgeH);
    const_cast<GuidedTargetOverlay*>(this)->m_calloutRectInHost = cardRect;
    if (cardRect.isEmpty()) {
        return;
    }

    const QColor textPrimary = light ? QColor(QStringLiteral("#0F172A")) : QColor(QStringLiteral("#F8FAFC"));

    QFont badgeFont = font();
    badgeFont.setPointSize(std::max(8, badgeFont.pointSize() - 1));
    badgeFont.setBold(true);

    QTextDocument doc;
    doc.setDocumentMargin(0.0);
    doc.setDefaultStyleSheet(
        QStringLiteral("body, div, b { color: %1; }").arg(textPrimary.name()));
    QFont docFont = font();
    docFont.setPointSize(fontPt);
    doc.setDefaultFont(docFont);
    doc.setHtml(QStringLiteral("<body>%1</body>").arg(m_unifiedHtmlBody));
    doc.setTextWidth(static_cast<qreal>(innerW));

    // Linha conectora + seta do cartão até o controle (ou borda da janela filha que contém o controle)
    const QRectF cardRectF(cardRect);
    QRectF arrowAnchorRect = haloRect;
    if (!m_showSpotlight && m_targetWidget && m_targetWidget->window() != m_hostWindow.data()) {
        QWidget* childWin = m_targetWidget->window();
        if (childWin && childWin->isVisible()) {
            const QRect cg = childWin->frameGeometry().united(
                QRect(childWin->mapToGlobal(QPoint(0, 0)), childWin->size()));
            const QRect mappedChild(m_hostWindow->mapFromGlobal(cg.topLeft()),
                                    m_hostWindow->mapFromGlobal(cg.bottomRight()));
            if (mappedChild.isValid()) {
                arrowAnchorRect = QRectF(mappedChild);
            }
        }
    }

    if (!cardRectF.intersects(arrowAnchorRect)) {
        const QPointF pCard = clampPointToRect(arrowAnchorRect.center(), cardRectF);
        const QPointF pTarget = clampPointToRect(cardRectF.center(), arrowAnchorRect);
        QPointF dir = pTarget - pCard;
        const qreal dist = std::hypot(dir.x(), dir.y());
        if (dist > 8.0) {
            dir /= dist;
            const QPointF perp(-dir.y(), dir.x());
            const qreal arrowLen = 9.0;
            const qreal arrowHalfW = 5.0;
            const QPointF base = pTarget - dir * arrowLen;

            p.setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(pCard, base);

            QPolygonF head;
            head << pTarget
                 << (base + perp * arrowHalfW)
                 << (base - perp * arrowHalfW);
            p.setBrush(accent);
            p.setPen(Qt::NoPen);
            p.drawPolygon(head);
        }
    }

    // Sombra suave + Fundo do cartão adaptado ao tema
    const QRectF shadowRect = cardRectF.translated(2.0, 3.0);
    p.setBrush(light ? QColor(15, 23, 42, 34) : QColor(0, 0, 0, 115));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(shadowRect, 8.0, 8.0);

    QColor cardBg = light
        ? (isPhase2 ? QColor(QStringLiteral("#ECFDF5")) : QColor(QStringLiteral("#FFFBEB")))
        : (isPhase2 ? QColor(QStringLiteral("#10221C")) : QColor(QStringLiteral("#231D12")));
    cardBg.setAlpha(244);

    p.setBrush(cardBg);
    p.setPen(QPen(accentStrong, 1.8));
    p.drawRoundedRect(cardRectF, 8.0, 8.0);

    // Cabeçalho colorido do Callout
    QPainterPath headerClip;
    headerClip.addRoundedRect(cardRectF, 8.0, 8.0);
    p.save();
    p.setClipPath(headerClip);
    p.fillRect(QRectF(cardRectF.left(), cardRectF.top(), cardRectF.width(), headerH), accentStrong);
    p.setFont(badgeFont);
    p.setPen(Qt::white);
    p.drawText(QRect(cardRect.left() + 11, cardRect.top() + 5, innerW, badgeH),
               Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter,
               m_badgeText);

    // Corpo Rich-Text unificado abaixo do cabeçalho colorido
    const qreal bodyTop = cardRectF.top() + headerH + padY;
    const qreal bodyAvailH = std::max<qreal>(12.0, cardRectF.bottom() - bodyTop - 6.0);
    p.translate(cardRectF.left() + 11.0, bodyTop);
    doc.drawContents(&p, QRectF(0.0, 0.0, static_cast<qreal>(innerW), bodyAvailH));
    p.restore();
}

// ============================================================================
// GuidedSessionDialog
// ============================================================================

GuidedSessionDialog::GuidedSessionDialog(MainWindow* mainWindow, QWidget* parent)
    : QDialog(parent),
      m_mainWindow(mainWindow),
      m_timer(new QTimer(this)),
      m_countdownTimer(new QTimer(this)),
      m_overlay(new GuidedTargetOverlay(nullptr)),
      m_childOverlay(new GuidedTargetOverlay(nullptr)) {
    setObjectName(QStringLiteral("GuidedSessionDialog"));
    setMinimumSize(540, 620);
    resize(560, 660);
    if (m_overlay) {
        m_overlay->setObjectName(QStringLiteral("guided_target_overlay"));
        m_overlay->setShowCalloutCard(true);
        m_overlay->setShowSpotlight(true);
    }
    if (m_childOverlay) {
        m_childOverlay->setObjectName(QStringLiteral("guided_child_target_overlay"));
        m_childOverlay->setShowCalloutCard(false);
        m_childOverlay->setShowSpotlight(true);
    }

    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &GuidedSessionDialog::onTimerTick);

    m_countdownTimer->setInterval(250);
    connect(m_countdownTimer, &QTimer::timeout, this, &GuidedSessionDialog::onCountdownTick);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(10);

    m_introLabel = new QLabel(this);
    m_introLabel->setWordWrap(true);
    rootLayout->addWidget(m_introLabel);

    // Configuração de Exemplo e Perfil Científico de Leitura (PPM)
    m_setupGroup = new QGroupBox(this);
    auto* setupLayout = new QVBoxLayout(m_setupGroup);
    setupLayout->setSpacing(6);

    m_exampleLabel = new QLabel(m_setupGroup);
    m_exampleLabel->setWordWrap(true);
    m_exampleCombo = new QComboBox(m_setupGroup);
    m_exampleCombo->addItem(QString(), 1);
    m_exampleCombo->addItem(QString(), 2);
    m_exampleCombo->addItem(QString(), 3);
    m_exampleCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_exampleCombo->setMinimumContentsLength(24);
    m_exampleCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_exampleCombo->setObjectName(QStringLiteral("guided_example_combo"));

    m_readingProfileLabel = new QLabel(m_setupGroup);
    m_readingProfileLabel->setWordWrap(true);
    m_readingProfileCombo = new QComboBox(m_setupGroup);
    m_readingProfileCombo->addItem(QString(), static_cast<int>(ReadingProfile::FluentTypical));
    m_readingProfileCombo->addItem(QString(), static_cast<int>(ReadingProfile::TechnicalTypical));
    m_readingProfileCombo->addItem(QString(), static_cast<int>(ReadingProfile::ReadAloud));
    m_readingProfileCombo->addItem(QString(), static_cast<int>(ReadingProfile::DyslexiaAccessible));
    m_readingProfileCombo->setCurrentIndex(static_cast<int>(ReadingProfile::TechnicalTypical));
    m_readingProfileCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_readingProfileCombo->setMinimumContentsLength(24);
    m_readingProfileCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_readingProfileCombo->setObjectName(QStringLiteral("guided_reading_profile_combo"));
    connect(m_readingProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx >= 0 && m_readingProfileCombo) {
            m_readingProfile = static_cast<ReadingProfile>(m_readingProfileCombo->itemData(idx).toInt());
            updateReadingInfoLabel();
            refreshCurrentStepTexts();
        }
    });

    m_readingInfoLabel = new QLabel(m_setupGroup);
    m_readingInfoLabel->setWordWrap(true);
    m_readingInfoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_readingInfoLabel->setObjectName(QStringLiteral("guided_reading_info"));

    setupLayout->addWidget(m_exampleLabel);
    setupLayout->addWidget(m_exampleCombo);
    setupLayout->addWidget(m_readingProfileLabel);
    setupLayout->addWidget(m_readingProfileCombo);
    setupLayout->addWidget(m_readingInfoLabel);
    rootLayout->addWidget(m_setupGroup);

    // Painel de Progresso, Controle em Foco, Cronômetro e Cartão Sincronizado
    m_progressGroup = new QGroupBox(this);
    auto* progLayout = new QVBoxLayout(m_progressGroup);
    progLayout->setSpacing(8);

    m_progressBar = new QProgressBar(m_progressGroup);
    m_progressBar->setRange(0, kTotalSteps);
    m_progressBar->setValue(0);
    m_progressBar->setObjectName(QStringLiteral("guided_progress_bar"));
    progLayout->addWidget(m_progressBar);

    m_phaseBadgeLabel = new QLabel(m_progressGroup);
    m_phaseBadgeLabel->setWordWrap(true);
    m_phaseBadgeLabel->setObjectName(QStringLiteral("guided_phase_badge"));
    progLayout->addWidget(m_phaseBadgeLabel);

    m_timerCountdownLabel = new QLabel(m_progressGroup);
    m_timerCountdownLabel->setWordWrap(true);
    m_timerCountdownLabel->setObjectName(QStringLiteral("guided_countdown_label"));
    progLayout->addWidget(m_timerCountdownLabel);

    auto* bannerScroll = new QScrollArea(m_progressGroup);
    bannerScroll->setFrameShape(QFrame::NoFrame);
    bannerScroll->setWidgetResizable(true);
    bannerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bannerScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    bannerScroll->setMinimumHeight(185);
    bannerScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    if (bannerScroll->viewport()) {
        bannerScroll->viewport()->setAutoFillBackground(false);
        bannerScroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    }

    m_targetActionBannerLabel = new QLabel(bannerScroll);
    m_targetActionBannerLabel->setWordWrap(true);
    m_targetActionBannerLabel->setTextFormat(Qt::RichText);
    m_targetActionBannerLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_targetActionBannerLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    m_targetActionBannerLabel->setObjectName(QStringLiteral("guided_target_action_banner"));
    bannerScroll->setWidget(m_targetActionBannerLabel);
    progLayout->addWidget(bannerScroll, 1);

    // Mantém os widgets legados ocultos para que apenas a versão moderna unificada seja exibida
    m_stepTitleLabel = new QLabel(m_progressGroup);
    m_stepTitleLabel->setObjectName(QStringLiteral("guided_step_title"));
    m_stepTitleLabel->hide();

    m_tooltipCardLabel = new QLabel(m_progressGroup);
    m_tooltipCardLabel->setObjectName(QStringLiteral("guided_tooltip_card"));
    m_tooltipCardLabel->hide();

    rootLayout->addWidget(m_progressGroup, 1);

    // Botões de Controle organizados em duas linhas para evitar qualquer truncamento de texto
    auto* btnContainer = new QVBoxLayout();
    btnContainer->setSpacing(6);
    auto* primaryBtnRow = new QHBoxLayout();
    primaryBtnRow->setSpacing(8);
    auto* secondaryBtnRow = new QHBoxLayout();
    secondaryBtnRow->setSpacing(8);

    m_btnStart = new QPushButton(this);
    m_btnStart->setObjectName(QStringLiteral("guided_btn_start"));
    m_btnStart->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_btnPauseResume = new QPushButton(this);
    m_btnPauseResume->setObjectName(QStringLiteral("guided_btn_pause"));
    m_btnPauseResume->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_btnNextStep = new QPushButton(this);
    m_btnNextStep->setObjectName(QStringLiteral("guided_btn_next"));
    m_btnNextStep->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    m_btnStop = new QPushButton(this);
    m_btnStop->setObjectName(QStringLiteral("guided_btn_stop"));
    m_btnStop->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_btnClose = new QPushButton(this);
    m_btnClose->setObjectName(QStringLiteral("guided_btn_close"));
    m_btnClose->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    connect(m_btnStart, &QPushButton::clicked, this, &GuidedSessionDialog::onStartClicked);
    connect(m_btnPauseResume, &QPushButton::clicked, this, &GuidedSessionDialog::onPauseResumeClicked);
    connect(m_btnNextStep, &QPushButton::clicked, this, &GuidedSessionDialog::onNextStepClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &GuidedSessionDialog::onStopClicked);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::close);

    primaryBtnRow->addWidget(m_btnStart);
    primaryBtnRow->addWidget(m_btnPauseResume);
    primaryBtnRow->addWidget(m_btnNextStep);
    secondaryBtnRow->addWidget(m_btnStop);
    secondaryBtnRow->addStretch(1);
    secondaryBtnRow->addWidget(m_btnClose);
    btnContainer->addLayout(primaryBtnRow);
    btnContainer->addLayout(secondaryBtnRow);
    rootLayout->addLayout(btnContainer);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this](UiTheme) {
        applyCardTheme();
        if (m_overlay) {
            m_overlay->refreshTheme();
        }
        if (m_childOverlay) {
            m_childOverlay->refreshTheme();
        }
    });

    retranslateUi();
    applyCardTheme();
    updateControlsState();
}

GuidedSessionDialog::~GuidedSessionDialog() {
    if (m_timer && m_timer->isActive()) m_timer->stop();
    if (m_countdownTimer && m_countdownTimer->isActive()) m_countdownTimer->stop();
    clearVisualHighlight();
    if (m_overlay) {
        delete m_overlay.data();
    }
    if (m_childOverlay) {
        delete m_childOverlay.data();
    }
    QToolTip::hideText();
}

GuidedSessionDialog::ReadingProfileSpec GuidedSessionDialog::profileSpec(ReadingProfile profile) const {
    switch (profile) {
        case ReadingProfile::FluentTypical:
            return {250, 1.00, 7000, 35000, 3500};
        case ReadingProfile::TechnicalTypical:
            return {150, 1.10, 10000, 42000, 4500};
        case ReadingProfile::ReadAloud:
            return {155, 1.12, 11000, 44000, 5000};
        case ReadingProfile::DyslexiaAccessible:
        default:
            return {130, 1.30, 15000, 60000, 6500};
    }
}

int GuidedSessionDialog::wordCountForStep(int step) const {
    using locale::GerenciadorTraducao;
    const StepSpec spec = specForStep(std::clamp(step, 1, kTotalSteps));
    const QString fullText = QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10")
        .arg(GerenciadorTraducao::trText("Etapa:"),
             GerenciadorTraducao::trText(spec.titlePtBr),
             GerenciadorTraducao::trText(spec.menuPathPtBr),
             GerenciadorTraducao::trText("Controle em Foco:"),
             GerenciadorTraducao::trText(spec.targetWidgetNamePtBr),
             GerenciadorTraducao::trText("Execução Atual:"),
             GerenciadorTraducao::trText(spec.actionSummaryPtBr),
             GerenciadorTraducao::trText("O que faz:"),
             GerenciadorTraducao::trText(spec.whatDoesPtBr),
             GerenciadorTraducao::trText("Para que serve:"))
        + QLatin1Char(' ')
        + GerenciadorTraducao::trText(spec.purposePtBr);
    const QStringList words = fullText.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    return std::max(1, static_cast<int>(words.size()));
}

int GuidedSessionDialog::currentStepWordCount() const {
    return wordCountForStep(m_currentStep > 0 ? m_currentStep : 1);
}

int GuidedSessionDialog::computeReadingDurationMsForStep(int step) const {
    const ReadingProfileSpec spec = profileSpec(m_readingProfile);
    const int words = wordCountForStep(step);
    const double rawMs = (static_cast<double>(words) / static_cast<double>(spec.ppm)) * 60000.0 * spec.cognitiveMultiplier;
    return std::clamp(static_cast<int>(std::lround(rawMs)), spec.minPhase1Ms, spec.maxPhase1Ms);
}

int GuidedSessionDialog::computePhase1ReadingMs(int step) const {
    if (m_running && m_customPhaseIntervalMs > 0) {
        return std::max(10, m_customPhaseIntervalMs);
    }
    return computeReadingDurationMsForStep(step);
}

int GuidedSessionDialog::computePhase2ObservationMs(int step) const {
    if (m_running && m_customPhaseIntervalMs > 0) {
        return std::max(10, m_customPhaseIntervalMs / 2);
    }
    const int targetStep = (step > 0) ? step : (m_currentStep > 0 ? m_currentStep : 1);
    return computeReadingDurationMsForStep(targetStep);
}

void GuidedSessionDialog::selectReadingProfile(ReadingProfile profile) {
    m_readingProfile = profile;
    if (m_readingProfileCombo) {
        const int idx = m_readingProfileCombo->findData(static_cast<int>(profile));
        if (idx >= 0) {
            m_readingProfileCombo->setCurrentIndex(idx);
        }
    }
    updateReadingInfoLabel();
    refreshCurrentStepTexts();
}

GuidedSessionDialog::ReadingProfile GuidedSessionDialog::selectedReadingProfile() const {
    return m_readingProfile;
}

QWidget* GuidedSessionDialog::currentHighlightedTarget() const {
    return m_currentHighlightedWidget.data();
}

QRect GuidedSessionDialog::currentHighlightedRectInWindow() const {
    if (m_childOverlay && m_childOverlay->isVisible() && !m_childOverlay->targetRectInHost().isEmpty()) {
        return m_childOverlay->targetRectInHost();
    }
    if (m_overlay && m_overlay->isVisible()) {
        return m_overlay->targetRectInHost();
    }
    return m_currentHighlightRect;
}

QRect GuidedSessionDialog::currentCalloutCardGlobalRect() const {
    if (m_overlay && m_overlay->isVisible()) {
        return m_overlay->lastCalloutCardGlobalRect();
    }
    return QRect();
}

bool GuidedSessionDialog::isTargetHighlightVisible() const {
    return (m_overlay && m_overlay->isVisible() && !m_overlay->targetRectInHost().isEmpty())
        || (m_childOverlay && m_childOverlay->isVisible() && !m_childOverlay->targetRectInHost().isEmpty());
}

void GuidedSessionDialog::positionHudNearMainWindow() {
    if (!m_mainWindow) return;
    ensurePolished();
    if (layout()) {
        layout()->activate();
    }
    const QRect mainRect = m_mainWindow->frameGeometry();
    const QScreen* screen = m_mainWindow->screen() ? m_mainWindow->screen() : QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : mainRect;

    const QSize desiredSize = size()
                                  .expandedTo(sizeHint())
                                  .expandedTo(minimumSizeHint())
                                  .expandedTo(QSize(560, 660));
    const int targetW = std::min(desiredSize.width(), std::max(540, avail.width() - 32));
    const int targetH = std::min(desiredSize.height(), std::max(560, avail.height() - 32));
    if (size() != QSize(targetW, targetH)) {
        resize(targetW, targetH);
    }

    int targetX = mainRect.right() - width() - 16;
    int targetY = mainRect.top() + 84;
    targetX = std::clamp(targetX, avail.left() + 8, std::max(avail.left() + 8, avail.right() - width() - 8));
    targetY = std::clamp(targetY, avail.top() + 8, std::max(avail.top() + 8, avail.bottom() - height() - 8));
    move(targetX, targetY);
}

void GuidedSessionDialog::applyCardTheme() {
    const bool light = ThemeManager::instance().isLight();
    const QString cardBg = light ? QStringLiteral("#FFFDF5") : QStringLiteral("#1F2633");
    const QString cardBorder = light ? QStringLiteral("#C5A34E") : QStringLiteral("#4C78B8");
    const QString cardText = light ? QStringLiteral("#1F242D") : QStringLiteral("#EDF2FA");
    m_tooltipCardLabel->setStyleSheet(
        QStringLiteral("QLabel { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 10px; color: %3; }")
            .arg(cardBg, cardBorder, cardText));

    const QString infoColor = light ? QStringLiteral("#4A5568") : QStringLiteral("#9FB3C8");
    m_readingInfoLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; }").arg(infoColor));
    m_timerCountdownLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-weight: bold; }")
            .arg(light ? QStringLiteral("#1E4E8C") : QStringLiteral("#8AC4FF")));

    QString bannerBg;
    QString bannerBorder;
    QString bannerText;
    if (!m_running && !m_completed) {
        bannerBg = light ? QStringLiteral("#F1F5F9") : QStringLiteral("#232A36");
        bannerBorder = light ? QStringLiteral("#94A3B8") : QStringLiteral("#475569");
        bannerText = light ? QStringLiteral("#1E293B") : QColor(226, 232, 240).name();
    } else if (m_completed) {
        bannerBg = light ? QStringLiteral("#ECFDF5") : QStringLiteral("#10271E");
        bannerBorder = light ? QStringLiteral("#059669") : QStringLiteral("#34D399");
        bannerText = light ? QStringLiteral("#064E3B") : QStringLiteral("#D1FAE5");
    } else if (m_currentPhase == 1) {
        bannerBg = light ? QStringLiteral("#FFFBEB") : QStringLiteral("#2A2112");
        bannerBorder = light ? QStringLiteral("#D97706") : QStringLiteral("#FBBF24");
        bannerText = light ? QStringLiteral("#78350F") : QStringLiteral("#FEF3C7");
    } else {
        bannerBg = light ? QStringLiteral("#ECFDF5") : QStringLiteral("#112620");
        bannerBorder = light ? QStringLiteral("#059669") : QStringLiteral("#34D399");
        bannerText = light ? QStringLiteral("#064E3B") : QStringLiteral("#D1FAE5");
    }
    if (m_targetActionBannerLabel) {
        m_targetActionBannerLabel->setStyleSheet(
            QStringLiteral("QLabel { background-color: %1; border-left: 4px solid %2; border-top: 1px solid %2; "
                           "border-right: 1px solid %2; border-bottom: 1px solid %2; border-radius: 5px; padding: 7px 9px; color: %3; }")
                .arg(bannerBg, bannerBorder, bannerText));
    }

    QString badgeStyle;
    if (!m_running && !m_completed) {
        badgeStyle = light
            ? QStringLiteral("QLabel { background-color: #E8ECF2; color: #2D3748; border-radius: 4px; padding: 4px 8px; font-weight: bold; }")
            : QStringLiteral("QLabel { background-color: #2B3340; color: #CBD5E1; border-radius: 4px; padding: 4px 8px; font-weight: bold; }");
    } else if (m_completed) {
        badgeStyle = QStringLiteral("QLabel { background-color: #1E6B43; color: #FFFFFF; border-radius: 4px; padding: 4px 8px; font-weight: bold; }");
    } else if (m_paused) {
        badgeStyle = QStringLiteral("QLabel { background-color: #8A5A00; color: #FFFFFF; border-radius: 4px; padding: 4px 8px; font-weight: bold; }");
    } else if (m_currentPhase == 1) {
        badgeStyle = light
            ? QStringLiteral("QLabel { background-color: #B45309; color: #FFFFFF; border-radius: 4px; padding: 4px 8px; font-weight: bold; }")
            : QStringLiteral("QLabel { background-color: #9B5E00; color: #FFFFFF; border-radius: 4px; padding: 4px 8px; font-weight: bold; }");
    } else {
        badgeStyle = light
            ? QStringLiteral("QLabel { background-color: #047857; color: #FFFFFF; border-radius: 4px; padding: 4px 8px; font-weight: bold; }")
            : QStringLiteral("QLabel { background-color: #059669; color: #FFFFFF; border-radius: 4px; padding: 4px 8px; font-weight: bold; }");
    }
    m_phaseBadgeLabel->setStyleSheet(badgeStyle);
}

void GuidedSessionDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (!event) return;
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        applyCardTheme();
        if (m_overlay) {
            m_overlay->refreshTheme();
        }
        if (m_childOverlay) {
            m_childOverlay->refreshTheme();
        }
    }
}

void GuidedSessionDialog::closeEvent(QCloseEvent* event) {
    stopSession();
    QDialog::closeEvent(event);
}

void GuidedSessionDialog::moveEvent(QMoveEvent* event) {
    QDialog::moveEvent(event);
    if (m_overlay && m_overlay->isVisible()) {
        m_overlay->syncAndUpdate();
    }
}

void GuidedSessionDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (m_overlay && m_overlay->isVisible()) {
        m_overlay->syncAndUpdate();
    }
}

void GuidedSessionDialog::updateReadingInfoLabel() {
    using locale::GerenciadorTraducao;
    if (!m_readingInfoLabel) return;
    QString info;
    switch (m_readingProfile) {
        case ReadingProfile::FluentTypical:
            info = GerenciadorTraducao::trText(
                "Leitor típico silencioso (250 PPM, faixa 200–300 PPM): reconhecimento visual lexical rápido, fixações curtas (~220 ms) e ritmo contínuo.");
            break;
        case ReadingProfile::TechnicalTypical:
            info = GerenciadorTraducao::trText(
                "Leitura técnica de engenharia (150 PPM, faixa 100–200 PPM): processamento conceitual denso com margem +10% para análise técnica no modelo 3D.");
            break;
        case ReadingProfile::ReadAloud:
            info = GerenciadorTraducao::trText(
                "Leitura em voz alta / instrucional (155 PPM, faixa 130–180 PPM): acompanha a cadência de articulação falada em apresentações ou aulas.");
            break;
        case ReadingProfile::DyslexiaAccessible:
        default:
            info = GerenciadorTraducao::trText(
                "Acessível para dislexia (130 PPM, faixa 100–160 PPM): decodificação cuidadosa, fixações prolongadas e fator +30% para regressões/releitura sem fadiga cognitiva.");
            break;
    }
    m_readingInfoLabel->setText(info);
}

void GuidedSessionDialog::updateCountdownDisplay() {
    using locale::GerenciadorTraducao;
    if (!m_timerCountdownLabel) return;

    if (!m_running) {
        if (m_completed) {
            m_timerCountdownLabel->setText(
                GerenciadorTraducao::trText("Concluído: 30 de 30 etapas demonstradas com reconstrução completa do zero."));
        } else {
            const int previewSec = static_cast<int>(std::lround(computePhase1ReadingMs(1) / 1000.0));
            m_timerCountdownLabel->setText(
                GerenciadorTraducao::trText("Estimativa inicial por etapa: ~%1 s de leitura calma (%2 PPM configurados).")
                    .arg(previewSec)
                    .arg(profileSpec(m_readingProfile).ppm));
        }
        return;
    }

    const int remMs = m_paused ? m_pausedRemainingMs
                               : std::max(0, m_currentPhaseDurationMs - static_cast<int>(m_phaseElapsed.elapsed()));
    const QString secStr = QString::number(remMs / 1000.0, 'f', 1);
    const int words = currentStepWordCount();
    const int ppm = profileSpec(m_readingProfile).ppm;

    if (m_paused) {
        m_timerCountdownLabel->setText(
            GerenciadorTraducao::trText("Pausado | Palavras do tooltip: %1 | Ritmo: %2 PPM | Restante: %3 s")
                .arg(words)
                .arg(ppm)
                .arg(secStr));
    } else if (m_currentPhase == 1) {
        m_timerCountdownLabel->setText(
            GerenciadorTraducao::trText("Tempo de leitura do tooltip: %1 palavras a %2 PPM | Próxima ação em: %3 s")
                .arg(words)
                .arg(ppm)
                .arg(secStr));
    } else {
        m_timerCountdownLabel->setText(
            GerenciadorTraducao::trText("Tempo de leitura/observação da execução: %1 palavras a %2 PPM | Próxima etapa em: %3 s")
                .arg(words)
                .arg(ppm)
                .arg(secStr));
    }
}

void GuidedSessionDialog::retranslateUi() {
    using locale::GerenciadorTraducao;
    setWindowTitle(GerenciadorTraducao::trText("Sessão Guiada Interativa — OmniFrame 3D"));
    m_introLabel->setText(GerenciadorTraducao::trText(
        "Escolha um dos três exemplos pré-definidos e o ritmo científico de leitura (PPM). "
        "O software partirá de um projeto vazio do zero e montará o exemplo etapa por etapa (30 etapas), "
        "destacando visualmente o controle em foco e explicando cada ação antes de executá-la."));

    m_setupGroup->setTitle(GerenciadorTraducao::trText("Configuração da Sessão Guiada e Ritmo Cognitivo"));
    m_exampleLabel->setText(GerenciadorTraducao::trText("Exemplo a Reconstruir:"));
    if (m_exampleCombo && m_exampleCombo->count() >= 3) {
        m_exampleCombo->setItemText(0, GerenciadorTraducao::trText("Exemplo 1: Pórtico Espacial de Edifício (Reconstrução do Zero)"));
        m_exampleCombo->setItemText(1, GerenciadorTraducao::trText("Exemplo 2: Balanço Espacial em L (Reconstrução do Zero)"));
        m_exampleCombo->setItemText(2, GerenciadorTraducao::trText("Exemplo 3: Treliça Espacial Passarela 3D (Reconstrução do Zero)"));
    }

    m_readingProfileLabel->setText(GerenciadorTraducao::trText("Ritmo de Leitura (PPM):"));
    if (m_readingProfileCombo && m_readingProfileCombo->count() >= 4) {
        m_readingProfileCombo->setItemText(0, GerenciadorTraducao::trText("Leitor Típico — Fluente (250 PPM | 200–300 PPM)"));
        m_readingProfileCombo->setItemText(1, GerenciadorTraducao::trText("Leitura Técnica / Engenharia (150 PPM | 100–200 PPM)"));
        m_readingProfileCombo->setItemText(2, GerenciadorTraducao::trText("Leitura em Voz Alta (155 PPM | 130–180 PPM)"));
        m_readingProfileCombo->setItemText(3, GerenciadorTraducao::trText("Dislexia / Acessível (130 PPM | 100–160 PPM + Regressão)"));
    }
    updateReadingInfoLabel();

    m_progressGroup->setTitle(GerenciadorTraducao::trText("Execução Passo a Passo e Tooltip Explicativo"));
    m_btnStart->setText(GerenciadorTraducao::trText("Iniciar Sessão Guiada"));
    m_btnPauseResume->setText(m_paused ? GerenciadorTraducao::trText("Continuar") : GerenciadorTraducao::trText("Pausar"));
    m_btnNextStep->setText(GerenciadorTraducao::trText("Avançar Agora"));
    m_btnStop->setText(GerenciadorTraducao::trText("Interromper"));
    m_btnClose->setText(GerenciadorTraducao::trText("Fechar"));

    refreshCurrentStepTexts();
    if (isVisible()) {
        if (layout()) {
            layout()->activate();
        }
        const QSize needed = size().expandedTo(sizeHint()).expandedTo(minimumSizeHint());
        if (needed != size()) {
            resize(needed);
        }
    }
    if (m_running && m_currentStep > 0) {
        updateVisualHighlight();
    }
}

void GuidedSessionDialog::selectExampleByNumber(int exampleNum) {
    if (!m_exampleCombo) return;
    const int idx = m_exampleCombo->findData(std::clamp(exampleNum, 1, 3));
    if (idx >= 0) {
        m_exampleCombo->setCurrentIndex(idx);
    }
}

int GuidedSessionDialog::selectedExampleNumber() const {
    if (!m_exampleCombo || m_exampleCombo->currentIndex() < 0) return 1;
    return m_exampleCombo->currentData().toInt();
}

void GuidedSessionDialog::schedulePhaseTimer(int durationMs) {
    m_currentPhaseDurationMs = std::max(10, durationMs);
    m_pausedRemainingMs = m_currentPhaseDurationMs;
    m_phaseElapsed.restart();
    if (m_running && !m_paused && m_timer) {
        m_timer->start(m_currentPhaseDurationMs);
    }
    if (m_running && !m_paused && m_countdownTimer && !m_countdownTimer->isActive()) {
        m_countdownTimer->start();
    }
    updateCountdownDisplay();
}

void GuidedSessionDialog::startSession(int customPhaseIntervalMs) {
    if (!m_mainWindow) return;
    if (m_timer && m_timer->isActive()) m_timer->stop();
    if (m_countdownTimer && m_countdownTimer->isActive()) m_countdownTimer->stop();

    clearVisualHighlight();
    closeAuxiliaryInputDialogs();
    m_activeDialogStep = 0;
    m_customPhaseIntervalMs = (customPhaseIntervalMs > 0) ? customPhaseIntervalMs : -1;

    m_running = true;
    m_paused = false;
    m_completed = false;
    m_currentStep = 1;
    m_currentPhase = 1;
    m_progressBar->setValue(0);

    if (m_mainWindow) {
        m_mainWindow->setInspectorDockCollapsed(false);
        m_mainWindow->setResultsDockCollapsed(false);
    }

    updateControlsState();
    showPreActionTooltipForStep(m_currentStep);
    schedulePhaseTimer(computePhase1ReadingMs(m_currentStep));
}

void GuidedSessionDialog::pauseOrResumeSession() {
    if (!m_running) return;
    m_paused = !m_paused;
    if (m_paused) {
        if (m_timer && m_timer->isActive()) m_timer->stop();
        if (m_countdownTimer && m_countdownTimer->isActive()) m_countdownTimer->stop();
        m_pausedRemainingMs = std::max(10, m_currentPhaseDurationMs - static_cast<int>(m_phaseElapsed.elapsed()));
    } else {
        m_currentPhaseDurationMs = std::max(10, m_pausedRemainingMs);
        m_phaseElapsed.restart();
        if (m_timer) m_timer->start(m_currentPhaseDurationMs);
        if (m_countdownTimer) m_countdownTimer->start();
    }
    refreshCurrentStepTexts();
    updateControlsState();
    updateVisualHighlight();
}

void GuidedSessionDialog::stopSession() {
    const bool wasRunning = m_running;
    m_running = false;
    m_paused = false;
    m_customPhaseIntervalMs = -1;
    if (m_timer && m_timer->isActive()) m_timer->stop();
    if (m_countdownTimer && m_countdownTimer->isActive()) m_countdownTimer->stop();
    clearVisualHighlight();
    QToolTip::hideText();
    closeAuxiliaryInputDialogs();
    m_activeDialogStep = 0;
    if (wasRunning && m_mainWindow) {
        m_mainWindow->showStatusMessage(locale::GerenciadorTraducao::trText("Sessão Guiada interrompida pelo usuário."));
    }
    refreshCurrentStepTexts();
    updateControlsState();
}

void GuidedSessionDialog::advancePhaseNow() {
    if (!m_running || m_executingStep) return;
    if (m_timer && m_timer->isActive()) m_timer->stop();
    onTimerTick();
}

void GuidedSessionDialog::onStartClicked() {
    startSession(-1);
}

void GuidedSessionDialog::onPauseResumeClicked() {
    pauseOrResumeSession();
}

void GuidedSessionDialog::onNextStepClicked() {
    if (!m_running) return;
    if (m_paused) {
        m_paused = false;
        updateControlsState();
    }
    advancePhaseNow();
}

void GuidedSessionDialog::onStopClicked() {
    stopSession();
}

void GuidedSessionDialog::onCountdownTick() {
    if (!m_running) {
        if (m_countdownTimer && m_countdownTimer->isActive()) m_countdownTimer->stop();
        return;
    }
    updateCountdownDisplay();
    if (m_overlay && m_overlay->isVisible()) {
        m_overlay->advancePulse();
    }
    if (m_childOverlay && m_childOverlay->isVisible()) {
        m_childOverlay->advancePulse();
    }
}

void GuidedSessionDialog::onTimerTick() {
    if (!m_running || m_paused || m_executingStep || !m_mainWindow) {
        return;
    }
    m_executingStep = true;

    if (m_currentPhase == 1) {
        // Fase 2: executa a ação correspondente mantendo eventual diálogo aberto e destacando o alvo ativo
        m_currentPhase = 2;
        refreshCurrentStepTexts();
        updateControlsState();
        pumpUiEvents(12);

        executeStepAction(m_currentStep);
        if (!m_running) {
            m_executingStep = false;
            return;
        }
        m_progressBar->setValue(m_currentStep);

        if (m_currentStep >= kTotalSteps) {
            updateVisualHighlight();
            commitPendingStepEdits(m_currentStep);
            finishSession();
            m_executingStep = false;
            return;
        }

        updateVisualHighlight();
        schedulePhaseTimer(computePhase2ObservationMs(m_currentStep));
    } else {
        // Conclui/aplica silenciosamente a janela da etapa anterior antes de explicar a próxima
        clearVisualHighlight();
        commitPendingStepEdits(m_currentStep);

        ++m_currentStep;
        if (m_currentStep > kTotalSteps) {
            finishSession();
            m_executingStep = false;
            return;
        }
        m_currentPhase = 1;
        showPreActionTooltipForStep(m_currentStep);
        updateControlsState();
        updateVisualHighlight();
        schedulePhaseTimer(computePhase1ReadingMs(m_currentStep));
    }

    m_executingStep = false;
}

void GuidedSessionDialog::updateControlsState() {
    m_exampleCombo->setEnabled(!m_running);
    m_readingProfileCombo->setEnabled(!m_running);
    m_btnStart->setEnabled(!m_running);
    m_btnPauseResume->setEnabled(m_running);
    m_btnNextStep->setEnabled(m_running);
    m_btnStop->setEnabled(m_running);
    m_btnPauseResume->setText(m_paused ? locale::GerenciadorTraducao::trText("Continuar") : locale::GerenciadorTraducao::trText("Pausar"));
    updateCountdownDisplay();
    applyCardTheme();
}

void GuidedSessionDialog::refreshCurrentStepTexts() {
    using locale::GerenciadorTraducao;

    if (m_completed) {
        m_phaseBadgeLabel->setText(GerenciadorTraducao::trText("CONCLUÍDA — TODAS AS 30 ETAPAS EXECUTADAS COM SUCESSO"));
        m_currentStepTitle = GerenciadorTraducao::trText("Sessão Guiada Finalizada (%1/%2)").arg(kTotalSteps).arg(kTotalSteps);
        const QString focusDone = GerenciadorTraducao::trText(
            "Concluído: Exemplo selecionado restaurado ao estado inicial no Viewport 3D.");
        const QString summaryDone = GerenciadorTraducao::trText(
            "O exemplo escolhido foi reconstruído integralmente do zero ao longo das 30 etapas e restaurado ao seu estado inicial padrão para uso imediato.");

        m_currentTargetActionText = focusDone;
        m_currentTooltipText = QStringLiteral("%1 %2\n%3 %4\n%5 %6")
                                   .arg(GerenciadorTraducao::trText("Etapa:"), m_currentStepTitle,
                                        GerenciadorTraducao::trText("Controle em Foco:"), focusDone,
                                        GerenciadorTraducao::trText("Resumo Final:"), summaryDone);

        m_currentUnifiedHtml = QStringLiteral(
            "<div><b>%1</b> %2</div>"
            "<div style='margin-top:4px;'><b>%3</b> %4</div>"
            "<div style='margin-top:5px;'><b>%5</b> %6</div>")
            .arg(GerenciadorTraducao::trText("Etapa:").toHtmlEscaped(),
                 m_currentStepTitle.toHtmlEscaped(),
                 GerenciadorTraducao::trText("Controle em Foco:").toHtmlEscaped(),
                 focusDone.toHtmlEscaped(),
                 GerenciadorTraducao::trText("Resumo Final:").toHtmlEscaped(),
                 summaryDone.toHtmlEscaped());
        if (m_targetActionBannerLabel) {
            m_targetActionBannerLabel->setText(m_currentUnifiedHtml);
        }
        m_stepTitleLabel->setText(m_currentStepTitle);
        m_tooltipCardLabel->setText(m_currentUnifiedHtml);
        updateCountdownDisplay();
        applyCardTheme();
        return;
    }

    if (m_currentStep <= 0) {
        m_phaseBadgeLabel->setText(GerenciadorTraducao::trText("AGUARDANDO INÍCIO — SELECIONE EXEMPLO E RITMO DE LEITURA"));
        m_currentStepTitle = GerenciadorTraducao::trText("Pronto para iniciar a Sessão Guiada (30 etapas do zero)");
        const QString focusIdle = GerenciadorTraducao::trText(
            "Menu Ajuda > Sessão Guiada (pronto para destacar controles em tempo real).");
        const QString howIdle = GerenciadorTraducao::trText(
            "Selecione o exemplo, ajuste o perfil de leitura (incluindo leitura técnica ou acessível para dislexia em PPM) e clique em \"Iniciar Sessão Guiada\". "
            "Cada etapa destacará visualmente o botão, menu ou painel correspondente com seta/callout e tooltip sincronizado.");

        m_currentTargetActionText = QStringLiteral("%1 %2")
                                        .arg(GerenciadorTraducao::trText("Controle em Foco:"), focusIdle);
        m_currentTooltipText = QStringLiteral("%1 %2\n%3 %4\n%5 %6")
                                   .arg(GerenciadorTraducao::trText("Status:"), m_currentStepTitle,
                                        GerenciadorTraducao::trText("Controle em Foco:"), focusIdle,
                                        GerenciadorTraducao::trText("Como funciona:"), howIdle);

        m_currentUnifiedHtml = QStringLiteral(
            "<div><b>%1</b> %2</div>"
            "<div style='margin-top:4px;'><b>%3</b> %4</div>"
            "<div style='margin-top:5px;'><b>%5</b> %6</div>")
            .arg(GerenciadorTraducao::trText("Status:").toHtmlEscaped(),
                 m_currentStepTitle.toHtmlEscaped(),
                 GerenciadorTraducao::trText("Controle em Foco:").toHtmlEscaped(),
                 focusIdle.toHtmlEscaped(),
                 GerenciadorTraducao::trText("Como funciona:").toHtmlEscaped(),
                 howIdle.toHtmlEscaped());
        if (m_targetActionBannerLabel) {
            m_targetActionBannerLabel->setText(m_currentUnifiedHtml);
        }
        m_stepTitleLabel->setText(m_currentStepTitle);
        m_tooltipCardLabel->setText(m_currentUnifiedHtml);
        updateCountdownDisplay();
        applyCardTheme();
        return;
    }

    const StepSpec spec = specForStep(m_currentStep);
    const QString titleTr = GerenciadorTraducao::trText(spec.titlePtBr);
    const QString menuTr = GerenciadorTraducao::trText(spec.menuPathPtBr);
    const QString whatTr = GerenciadorTraducao::trText(spec.whatDoesPtBr);
    const QString purposeTr = GerenciadorTraducao::trText(spec.purposePtBr);
    const QString targetTr = GerenciadorTraducao::trText(spec.targetWidgetNamePtBr);
    const QString actionTr = GerenciadorTraducao::trText(spec.actionSummaryPtBr);

    if (m_paused) {
        m_phaseBadgeLabel->setText(GerenciadorTraducao::trText("PAUSADA — CLIQUE EM CONTINUAR OU AVANÇAR AGORA"));
    } else if (m_currentPhase == 1) {
        m_phaseBadgeLabel->setText(GerenciadorTraducao::trText("FASE 1/2 — PRÓXIMO CONTROLE A ACIONAR (TOOLTIP + MIRA)"));
    } else {
        m_phaseBadgeLabel->setText(GerenciadorTraducao::trText("FASE 2/2 — EXECUTANDO NESTE CONTROLE / DIÁLOGO"));
    }

    m_currentStepTitle = GerenciadorTraducao::trText("Etapa %1 de %2: %3")
                             .arg(m_currentStep)
                             .arg(kTotalSteps)
                             .arg(titleTr);

    m_currentTargetActionText = QStringLiteral("%1 %2 | %3 %4")
                                    .arg(GerenciadorTraducao::trText("Controle em Foco:"), targetTr,
                                         GerenciadorTraducao::trText("Execução Atual:"), actionTr);

    m_currentTooltipText = QStringLiteral(
        "%1 %2\n%3 %4\n%5 %6\n%7 %8\n%9 %10\n%11 %12")
        .arg(GerenciadorTraducao::trText("Etapa:"), m_currentStepTitle,
             GerenciadorTraducao::trText("Caminho na GUI:"), menuTr,
             GerenciadorTraducao::trText("Controle em Foco:"), targetTr)
        .arg(GerenciadorTraducao::trText("Execução Atual:"), actionTr,
             GerenciadorTraducao::trText("O que faz:"), whatTr,
             GerenciadorTraducao::trText("Para que serve:"), purposeTr);

    m_currentUnifiedHtml = QStringLiteral(
        "<div><b>%1</b> %2</div>"
        "<div style='margin-top:3px;'><b>%3</b> %4</div>"
        "<div style='margin-top:3px;'><b>%5</b> %6</div>"
        "<div style='margin-top:3px;'><b>%7</b> %8</div>"
        "<div style='margin-top:5px;'><b>%9</b> %10</div>"
        "<div style='margin-top:3px;'><b>%11</b> %12</div>")
        .arg(GerenciadorTraducao::trText("Etapa:").toHtmlEscaped(),
             m_currentStepTitle.toHtmlEscaped(),
             GerenciadorTraducao::trText("Caminho na GUI:").toHtmlEscaped(),
             menuTr.toHtmlEscaped(),
             GerenciadorTraducao::trText("Controle em Foco:").toHtmlEscaped(),
             targetTr.toHtmlEscaped())
        .arg(GerenciadorTraducao::trText("Execução Atual:").toHtmlEscaped(),
             actionTr.toHtmlEscaped(),
             GerenciadorTraducao::trText("O que faz:").toHtmlEscaped(),
             whatTr.toHtmlEscaped(),
             GerenciadorTraducao::trText("Para que serve:").toHtmlEscaped(),
             purposeTr.toHtmlEscaped());

    if (m_targetActionBannerLabel) {
        m_targetActionBannerLabel->setText(m_currentUnifiedHtml);
    }
    m_stepTitleLabel->setText(m_currentStepTitle);
    m_tooltipCardLabel->setText(m_currentUnifiedHtml);
    updateCountdownDisplay();
    applyCardTheme();
}

GuidedSessionDialog::StepSpec GuidedSessionDialog::specForStep(int step) const {
    switch (step) {
        case 1:
            return {
                QStringLiteral("Novo Projeto do Zero e Limpeza Inicial da Área de Trabalho"),
                QStringLiteral("Zera nós, barras, cargas e resultados anteriores para iniciar uma folha limpa com o catálogo padrão carregado, preparando a montagem manual do exemplo escolhido."),
                QStringLiteral("Serve para comprovar todo o fluxo de concepção desde o arquivo vazio até a análise final e memorial ABNT NBR."),
                QStringLiteral("Arquivo > Novo Projeto / Menu Exemplos"),
                QStringLiteral("Menu Arquivo / Área Gráfica 3D"),
                QStringLiteral("Reinicia o projeto para 0 nós e 0 barras com catálogos padrão")
            };
        case 2:
            return {
                QStringLiteral("Menu Configurações: Qualidade Gráfica, SSAA/MSAA, Superfície e HUD"),
                QStringLiteral("Ajusta o perfil de renderização OpenGL, ativa a suavização analítica de linhas SDF, exibe a moldura do plano ativo e habilita o HUD de telemetria gráfica."),
                QStringLiteral("Serve para calibrar nitidez visual, desempenho da GPU e clareza do espaço de trabalho antes da modelagem 3D."),
                QStringLiteral("Configurações > Qualidade Gráfica / Aparência do Plano / Idioma e Tema"),
                QStringLiteral("Menu Configurações > Qualidade & Telemetria"),
                QStringLiteral("Ativa Perfil Alta Qualidade, SDF Anti-Aliasing, moldura do plano e HUD")
            };
        case 3:
            return {
                QStringLiteral("Barra de Ferramentas 1: Plano de Trabalho, Offset, Passo da Grade e Snap"),
                QStringLiteral("Seleciona o plano ativo XY (planta), define cota Z = 0,0 m nas caixas de seleção e ajusta o passo da grade com imã Snap 3D ativado."),
                QStringLiteral("Serve para alinhar a referência geométrica do piso térreo antes de posicionar cada coordenada nodal com precisão."),
                QStringLiteral("Barra de Ferramentas 1: Plano Ativo / Offset (m) / Grade (m) / Snap"),
                QStringLiteral("Combobox Plano Ativo (Barra 1)"),
                QStringLiteral("Define plano XY (Planta), cota 0,00 m, passo da grade e Snap 3D")
            };
        case 4:
            return {
                QStringLiteral("Catálogo de Materiais (Materiais...) e Rolagem da Biblioteca"),
                QStringLiteral("Abre o diálogo de Materiais, percorre a barra de rolagem da lista completa de materiais e seleciona o material normativo exigido pelo exemplo."),
                QStringLiteral("Serve para verificar módulo de elasticidade E, coeficiente de Poisson ν, peso específico ρ e resistências fck/fy antes de lançar as barras."),
                QStringLiteral("Estrutura > Materiais... (Barra de Ferramentas 2: Materiais...)"),
                QStringLiteral("Botão Materiais... > Lista de Materiais"),
                QStringLiteral("Abre diálogo modeless Materiais, rola lista e seleciona Concreto/Aço")
            };
        case 5:
            return {
                QStringLiteral("Catálogo de Seções Transversais (Seções...) e Rolagem de Perfis"),
                QStringLiteral("Abre a biblioteca de Seções Transversais, navega pela barra de rolagem entre pilares, vigas e tubos estruturais e destaca o perfil do modelo."),
                QStringLiteral("Serve para conferir áreas A, momentos de inércia Iy/Iz e rigidez à torção J das seções estruturais."),
                QStringLiteral("Estrutura > Seções Transversais... (Barra de Ferramentas 2: Seções...)"),
                QStringLiteral("Botão Seções... > Lista de Perfis"),
                QStringLiteral("Abre diálogo modeless Seções, rola 13 perfis e destaca a seção-base")
            };
        case 6:
            return {
                QStringLiteral("Modo Criar Nó e Entrada Rápida por Spinboxes (X, Y, Z) na Base Z = 0"),
                QStringLiteral("Ativa a ferramenta Criar Nó, preenche coordenadas nas spinboxes X, Y, Z do painel lateral e pressiona \"Adicionar Nó\" para inserir os primeiros nós da base."),
                QStringLiteral("Serve para lançar vértices de fundação, apoios ou primeiros módulos estruturais com coordenadas exatas no sistema global."),
                QStringLiteral("Barra 1: Criar Nó > Inspetor Lateral (Coordenadas X, Y, Z + Adicionar Nó)"),
                QStringLiteral("Botão Adicionar Nó e Spinboxes X, Y, Z (Inspetor)"),
                QStringLiteral("Lança os nós iniciais da base (Z = 0,0 m) via painel de entrada rápida")
            };
        case 7:
            return {
                QStringLiteral("Menu Editar (Desfazer / Refazer) e Ferramenta Excluir (Del)"),
                QStringLiteral("Insere um nó auxiliar temporário, demonstra o modo Excluir na Barra 1 e aplica Desfazer (Ctrl+Z) e Refazer (Ctrl+Y) passo a passo mantendo o histórico consistente."),
                QStringLiteral("Serve para corrigir erros de lançamento com segurança total durante a construção geométrica sem perder trabalho anterior."),
                QStringLiteral("Editar > Desfazer (Ctrl+Z) / Refazer (Ctrl+Y) e Barra 1: Excluir"),
                QStringLiteral("Menu Editar & Botão Excluir (Barra 1)"),
                QStringLiteral("Testa inserção auxiliar + modo Excluir + Undo/Redo restaurando nós exatos")
            };
        case 8:
            return {
                QStringLiteral("Avanço de Cota no Plano Ativo e Lançamento dos Nós Superiores"),
                QStringLiteral("Altera o offset de elevação do plano ativo na spinbox superior e lança os demais nós dos pavimentos superiores ou banzos da estrutura pelo painel rápido."),
                QStringLiteral("Serve para completar a malha nodal tridimensional exata em múltiplos níveis de elevação do projeto selecionado."),
                QStringLiteral("Barra 1: Offset (m) + Painel Entrada Rápida: Adicionar Nó"),
                QStringLiteral("Spinbox Cota do Plano Ativo (Barra 1)"),
                QStringLiteral("Avança cota Z dos pavimentos/banzos e conclui toda a malha nodal")
            };
        case 9:
            return {
                QStringLiteral("Ferramenta Criar Barra: Montagem dos Pilares ou Banzos Principais"),
                QStringLiteral("Seleciona a ferramenta Criar Barra (m_actAddElement) e conecta os primeiros elementos portantes principais (pilares verticais, trecho inicial ou banzos longitudinais)."),
                QStringLiteral("Serve para estruturar o esqueleto primário de sustentação vertical ou longitudinal ligando os pares de nós construídos."),
                QStringLiteral("Barra 1: Criar Barra > Conectividade Nodal Primária"),
                QStringLiteral("Botão Inserir Barra (B) na Barra 1"),
                QStringLiteral("Conecta pilares principais, primeiro braço ou banzos longitudinais")
            };
        case 10:
            return {
                QStringLiteral("Montagem das Barras Secundárias: Vigas, Balanço em L ou Diagonais/Travamentos"),
                QStringLiteral("Conecta vigas de pavimento e cobertura (Ex. 1), trecho perpendicular em Y (Ex. 2) ou diagonais, montantes e contraventamentos 3D (Ex. 3) completando toda a geometria."),
                QStringLiteral("Serve para fechar o sistema reticulado completo com exatamente todas as barras do exemplo escolhido."),
                QStringLiteral("Barra 1: Criar Barra > Vigas / Diagonais / Contraventamentos"),
                QStringLiteral("Botão Inserir Barra (B) / Viewport 3D"),
                QStringLiteral("Fecha vigas, balanço ortogonal ou diagonais e travamentos 3D")
            };
        case 11:
            return {
                QStringLiteral("Ferramentas Medir (Distância) e Eixos Locais (xL, yL, zL)"),
                QStringLiteral("Habilita as tríades de eixos locais nas barras e ativa a ferramenta Medir para conferir vãos, alturas de pavimento e orientação local dos perfis."),
                QStringLiteral("Serve para auditar comprimentos e eixos principais de inércia antes de aplicar vínculos e carregamentos direcionados."),
                QStringLiteral("Barra 1: Medir e Barra 2: Eixos Locais"),
                QStringLiteral("Botões Medir (M) e Eixos Locais (Barra 1)"),
                QStringLiteral("Exibe tríades locais (xL, yL, zL) nas barras e aciona ferramenta Medir")
            };
        case 12:
            return {
                QStringLiteral("Condições de Apoio nos Nós da Base (Engastes / Pinos / Roletes)"),
                QStringLiteral("Abre o diálogo de Condições de Apoio nos nós de fundação e configura engastamentos completos (6 GDL) ou apoios isostáticos articulados/móveis conforme o modelo."),
                QStringLiteral("Serve para garantir estabilidade cinemática 3D sem mecanismos e definir onde surgirão as reações de apoio."),
                QStringLiteral("Estrutura > Condições de Apoio... (Barra 2: Apoios...)"),
                QStringLiteral("Botão Apoios... > Diálogo Condições de Apoio"),
                QStringLiteral("Configura engastes / pinos / roletes nos nós de apoio da base")
            };
        case 13:
            return {
                QStringLiteral("Inspetor Contextual, Comboboxes Inline e Barra de Rolagem Vertical"),
                QStringLiteral("Seleciona uma barra e um nó no Painel Inspetor, aciona as listas suspensas Combobox de Material e Seção, move a barra de rolagem (QScrollArea) e inspeciona coordenadas."),
                QStringLiteral("Serve para editar propriedades de qualquer nó ou barra em um único painel lateral rolável de acesso rápido."),
                QStringLiteral("Painel Inspetor Lateral: Comboboxes + Barra de Rolagem"),
                QStringLiteral("Inspetor Lateral: Combobox Seção & Rolagem"),
                QStringLiteral("Seleciona barra/nó, alterna combobox inline de seção e rola inspetor")
            };
        case 14:
            return {
                QStringLiteral("Rótulas e Liberações de Extremidade (Engaste Contínuo ou Treliça Ideal)"),
                QStringLiteral("Abre o diálogo de Rótulas/Liberações e aplica nós rígidos contínuos (Exemplos 1 e 2) ou articulações de momentos My/Mz em todas as 57 barras da Treliça Espacial (Exemplo 3)."),
                QStringLiteral("Serve para modelar corretamente ligações soldadas/monolíticas ou nós treliçados sujeitos apenas a esforço axial."),
                QStringLiteral("Estrutura > Rótulas / Liberações... (Barra 2: Rótulas...)"),
                QStringLiteral("Botão Rótulas... > Presets de Liberação"),
                QStringLiteral("Aplica nós contínuos ou rótulas My/Mz de treliça espacial")
            };
        case 15:
            return {
                QStringLiteral("Cargas Nodais Concentradas (Vento Lateral ou Ação Nodal 3D)"),
                QStringLiteral("Abre o formulário de Cargas Nodais Concentradas e aplica ações horizontais de vento nos pavimentos (Ex. 1), carga flexo-torsora na ponta (Ex. 2) ou cargas no tabuleiro (Ex. 3)."),
                QStringLiteral("Serve para representar esforços concentrados pontuais (Fx, Fy, Fz, Mx, My, Mz) aplicados diretamente nos nós."),
                QStringLiteral("Estrutura > Cargas Nodais Concentradas... (Barra 2: Carga Nodal...)"),
                QStringLiteral("Botão Carga Nodal... > Diálogo de Forças/Momentos"),
                QStringLiteral("Define Fx, Fy, Fz concentrados nos nós carregados do exemplo")
            };
        case 16:
            return {
                QStringLiteral("Cargas Uniformemente Distribuídas ao Longo das Barras"),
                QStringLiteral("Abre o diálogo de Cargas Distribuídas para aplicar carregamento gravitacional qZ nas vigas de pavimento e cobertura (Ex. 1) ou inspecionar carregamentos distribuídos."),
                QStringLiteral("Serve para converter cargas de lajes, alvenarias e peso distribuído em ações equivalentes exatas nas barras."),
                QStringLiteral("Estrutura > Cargas Uniformemente Distribuídas... (Barra 2: Carga Distr....)"),
                QStringLiteral("Botão Carga Distr.... > Diálogo Carga Distribuída"),
                QStringLiteral("Aplica qZ gravitacional distribuído nas vigas ou demonstra formulário")
            };
        case 17:
            return {
                QStringLiteral("Cargas Térmicas Uniformes (ΔT) em Elementos de Barra"),
                QStringLiteral("Abre a janela de Variação Térmica (ΔT) demonstrando o ajuste do gradiente térmico em °C e a ativação de dilatação axial restringida para estudo normativo."),
                QStringLiteral("Serve para avaliar esforços axiais extras gerados por insolação ou variações térmicas em estruturas hiperestáticas."),
                QStringLiteral("Estrutura > Cargas Térmicas (ΔT)... (Barra 2: Térmica ΔT...)"),
                QStringLiteral("Botão Térmica (ΔT)... > Diálogo Gradiente Térmico"),
                QStringLiteral("Demonstra variação térmica ΔT = +15,0 °C em barras")
            };
        case 18:
            return {
                QStringLiteral("Ângulo de Roll da Seção Transversal (γ) no Sistema Local"),
                QStringLiteral("Abre o diálogo Ângulo de Roll (γ) demonstrando o giro controlado dos eixos principais locais da seção em graus ao redor do eixo longitudinal xL."),
                QStringLiteral("Serve para orientar pilares retangulares, perfis I/U ou cantoneiras rotacionadas na arquitetura do projeto."),
                QStringLiteral("Estrutura > Ângulo de Roll da Seção (γ)... (Barra 2: Roll γ...)"),
                QStringLiteral("Botão Roll (γ)... > Diálogo Ângulo de Roll"),
                QStringLiteral("Demonstra rotação de eixo principal da seção em γ graus")
            };
        case 19:
            return {
                QStringLiteral("Menu Exibir e Barra 1: Câmeras Ortogonais (XZ, XY, YZ), Isométrica e Enquadrar"),
                QStringLiteral("Percorre os botões e menu Exibir alternando Vista Frontal XZ, Planta XY, Lateral YZ, Isométrica 3D e Enquadrar Tudo (F) antes de disparar o cálculo."),
                QStringLiteral("Serve para conferir visibilidade 3D completa de nós, apoios, conectividade e vetores de carga em todas as projeções."),
                QStringLiteral("Exibir > Vistas Ortogonais / Enquadrar Tudo (F)"),
                QStringLiteral("Botões de Câmera (3D Iso / Frontal / Planta / Enquadrar)"),
                QStringLiteral("Alterna projeções XZ, XY, YZ e retorna enquadrado em 3D Isométrico")
            };
        case 20:
            return {
                QStringLiteral("Solucionador Estático MEF 3D (Calcular F5) e Recálculo Automático"),
                QStringLiteral("Ativa o Recálculo Automático e aciona o botão Calcular Estrutura (F5), montando e resolvendo o sistema global de rigidez esparsa 6 GDL por nó em segundo plano."),
                QStringLiteral("Serve para calcular deslocamentos nodais u/θ, reações de apoio e todos os esforços solicitantes internos N, Vy, Vz, Tx, My e Mz."),
                QStringLiteral("Barra 2: ▶ CALCULAR ESTRUTURA (F5) e Estrutura > Recálculo Automático"),
                QStringLiteral("Botão ▶ CALCULAR ESTRUTURA (F5)"),
                QStringLiteral("Resolve matriz de rigidez esparsa K·U = F e atualiza resultados")
            };
        case 21:
            return {
                QStringLiteral("Pós-Processamento Barra 2: Diagramas, Vetores de Reação e Slider de Escala"),
                QStringLiteral("Exibe as setas de reações de apoio, percorre a combobox de diagramas (Deformada 3D, Normal N, Momento Mz/Cortante Vz) e ajusta o controle deslizante (QSlider) de escala."),
                QStringLiteral("Serve para interpretar graficamente compressões, trações, pontos de momento máximo e equilíbrio de reações na tela 3D."),
                QStringLiteral("Barra 2: Reações / Combobox Diagrama / Controle Deslizante Escala"),
                QStringLiteral("Combobox Diagrama & Slider Escala (Barra 2)"),
                QStringLiteral("Alterna Deformada / N / Mz, exibe reações e ajusta slider de escala")
            };
        case 22:
            return {
                QStringLiteral("Painel Inferior de Resultados (QDockWidget): 4 Abas e Rolagem de Tabelas"),
                QStringLiteral("Foca o painel de resultados, alterna entre Deslocamentos, Reações de Apoio, Esforços Internos 1D e Esforços 2D/3D (Casca/Sólido), rolando as tabelas até o somatório de reações."),
                QStringLiteral("Serve para validar equilíbrio ΣF = 0 e ΣM = 0 e consultar valores máximos exatos em barras e elementos contínuos."),
                QStringLiteral("Painel Inferior: Deslocamentos / Reações / Esforços 1D / Esforços 2D/3D"),
                QStringLiteral("Abas de Resultados (4 Abas: Deslocamentos / Reações / Esforços / 2D/3D)"),
                QStringLiteral("Percorre as 4 abas de resultados e rola tabelas até a soma Σ Reações")
            };
        case 23:
            return {
                QStringLiteral("Casos de Carga, Combinações ABNT NBR, Vento NBR 6123 e NBR 6120"),
                QStringLiteral("Abre o diálogo de Combinações NBR, seleciona Concreto (NBR 6118) ou Aço (NBR 8800), gera combinações ELU/ELS e calcula pressão de vento e sobrecargas normativas."),
                QStringLiteral("Serve para ponderar automaticamente ações permanentes, variáveis e vento segundo as normas brasileiras vigentes."),
                QStringLiteral("Estrutura > Casos de Carga, Combinações NBR & Vento..."),
                QStringLiteral("Botão Combinações... > Abas NBR 6118/8800/6123"),
                QStringLiteral("Gera combinações ELU/ELS e calcula vento NBR 6123 e sobrecarga")
            };
        case 24:
            return {
                QStringLiteral("Gerador de Malha 1D/2D/3D E2E: Material, Espessura, Apoios, Carga e Tensões"),
                QStringLiteral("Abre o Gerador de Malha, enquadra os limites do modelo, configura material, espessura de casca, contorno engastado e pressão normal, gera a malha e resolve tensões 2D/3D na Aba 4."),
                QStringLiteral("Serve para integrar elementos finitos 2D/3D (Quad4/Tri3/Hex8/Tet4) ao solver MEF E2E e inspecionar σ_vM, σ_1, σ_3 e esforços Mxx/Nxx."),
                QStringLiteral("Estrutura > Gerador de Malha 1D/2D/3D... (Barra 2: Malha...)"),
                QStringLiteral("Botão Malha 2D/3D... > Gerador de Malha + Aba Esforços 2D/3D"),
                QStringLiteral("Gera malha Quad4 com espessura/apoios/pressão e resolve tensões E2E na Aba 4")
            };
        case 25:
            return {
                QStringLiteral("Suíte Avançada (Abas 1–3): Solver Esparso (LDLT/LU/PCG), Combinações, Modal e Flambagem"),
                QStringLiteral("Abre a Suíte Avançada, seleciona o solver linear esparso (SimplicialLDLT/SparseLU/PCG), resolve envoltórias NBR, frequências naturais (Modal) e fatores críticos de flambagem com suporte a imperfeições geométricas."),
                QStringLiteral("Serve para comparar solvers de sistemas esparsos, diagnosticar rigidez dinâmica global e verificar estabilidade elástica."),
                QStringLiteral("Barra 2: Avançadas... > Combinações / Modal / Flambagem"),
                QStringLiteral("Botão Avançadas... > Solver Linear & Abas Combinações / Modal / Flambagem"),
                QStringLiteral("Seleciona SimplicialLDLT, resolve Envoltória NBR, Modos e Flambagem λ_cr")
            };
        case 26:
            return {
                QStringLiteral("Suíte Avançada (Abas 4–5): Resposta Harmônica (FRF) e Espectro NBR 15421"),
                QStringLiteral("Na mesma janela aberta da Suíte Avançada, calcula a Função Resposta em Frequência (Harmônica) e a combinação modal espectral sísmica segundo a ABNT NBR 15421."),
                QStringLiteral("Serve para evitar ressonância sob excitações periódicas e quantificar picos de resposta sob sismo normativo brasileiro."),
                QStringLiteral("Barra 2: Avançadas... > Harmônica (FRF) / Espectro Sísmico"),
                QStringLiteral("Suíte de Análises > Abas Harmônica (FRF) e Espectro"),
                QStringLiteral("Resolve varredura de frequência FRF e espectro sísmico NBR 15421")
            };
        case 27:
            return {
                QStringLiteral("Suíte Avançada (Abas 6–7): Transiente HHT-α / Newmark-β e Não Linear Corrotacional 3D"),
                QStringLiteral("Executa a integração direta no tempo (Newmark-β / HHT-α, sismo ou histórico F(t)) na Aba 6 e o solver incremental não linear geométrico Newton-Raphson / Corrotacional 3D com Line Search na Aba 7."),
                QStringLiteral("Serve para avaliar histórico sísmico/transiente com amortecimento numérico e efeitos corrotacionais de grande rotação e segunda ordem nas barras 1D."),
                QStringLiteral("Barra 2: Avançadas... > Sísmica/F(t) HHT-α / Não Linear Corrotacional"),
                QStringLiteral("Suíte de Análises > Abas Transiente HHT-α e Não Linear Corrotacional"),
                QStringLiteral("Integra histórico HHT-α e resolve análise não linear corrotacional 3D")
            };
        case 28:
            return {
                QStringLiteral("Controles 3D Avançados: Contornos |u| e von Mises σ_vM, Malha, Corte Z e Animação"),
                QStringLiteral("Abre o controle de Pós-Processamento 3D, alterna mapas de contorno de deslocamentos |u| e tensão de von Mises da casca 2D, exibe a malha gerada, aplica plano de corte Z e demonstra a animação."),
                QStringLiteral("Serve para inspecionar concentrações de tensão σ_vM na malha contínua, avaliar seções de corte e animar a resposta deformada."),
                QStringLiteral("Estrutura > Controles 3D... (Barra 2: Corte/Contornos...)"),
                QStringLiteral("Botão Corte/Contornos... > Contorno von Mises & Controles 3D"),
                QStringLiteral("Ativa contornos |u| / von Mises (Casca/Sólido), malha, corte Z e animação")
            };
        case 29:
            return {
                QStringLiteral("Auditoria Normativa ABNT NBR (6118 / 8800 / 6122) e Mapa de Calor DCR 3D"),
                QStringLiteral("Abre a Auditoria Normativa NBR, executa verificação completa de barras, nós ELS e sapatas de fundação, rola o relatório e projeta o Mapa de Calor DCR 3D no Viewport."),
                QStringLiteral("Serve para atestar taxas de utilização Demand/Capacity Ratio (DCR <= 1,00) e identificar instantaneamente barras críticas."),
                QStringLiteral("Estrutura > Auditoria Normativa ABNT NBR... (Barra 2: Auditoria NBR...)"),
                QStringLiteral("Botão Auditoria NBR... > Projetar Mapa DCR 3D"),
                QStringLiteral("Audita conformidade ELU/ELS e aplica Mapa de Calor DCR no Viewport 3D")
            };
        case 30:
        default:
            return {
                QStringLiteral("Menu Arquivo e Entrega Final: Salvar, Memorial PDF A4, Relatórios HTML/Markdown, DXF e IFC4"),
                QStringLiteral("Encerra a montagem apresentando o modelo reconstruído completo com resultados 1D/2D/3D ativos e destaca o menu Arquivo para gravação .omniframe e exportação de memoriais e BIM."),
                QStringLiteral("Serve para documentar o cálculo estrutural com memorial A4 completo ou relatórios web/Markdown editáveis e continuar editando livremente o projeto."),
                QStringLiteral("Arquivo > Salvar / Exportar PDF A4 / HTML / Markdown / DXF / IFC4"),
                QStringLiteral("Menu Arquivo (Gravação e Exportação)"),
                QStringLiteral("Apresenta modelo 1D/2D completo pronto para salvar .omniframe ou exportar PDF/HTML/MD/BIM")
            };
    }
}

QWidget* GuidedSessionDialog::resolveTargetWidgetForStep(int step,
                                                         int phase,
                                                         QRect* subRectInTarget,
                                                         QPoint* customGlobalPos) const {
    if (!m_mainWindow) return nullptr;
    if (subRectInTarget) *subRectInTarget = QRect();

    auto tbWidget = [](QToolBar* tb, QAction* act) -> QWidget* {
        return (tb && act) ? tb->widgetForAction(act) : nullptr;
    };
    auto menuActionTarget = [&](QMenu* menu) -> QWidget* {
        if (m_mainWindow->menuBar() && menu) {
            const QRect r = m_mainWindow->menuBar()->actionGeometry(menu->menuAction());
            if (r.isValid()) {
                if (subRectInTarget) *subRectInTarget = r;
                if (customGlobalPos) *customGlobalPos = m_mainWindow->menuBar()->mapToGlobal(r.center());
            }
            return m_mainWindow->menuBar();
        }
        return m_mainWindow->m_viewport;
    };

    const int exNum = selectedExampleNumber();

    switch (step) {
        case 1:
            return (phase == 2) ? static_cast<QWidget*>(m_mainWindow->m_viewport) : menuActionTarget(m_mainWindow->m_fileMenu);
        case 2:
            return (phase == 2) ? static_cast<QWidget*>(m_mainWindow->m_viewport) : menuActionTarget(m_mainWindow->m_settingsMenu);
        case 3:
            return firstVisibleWidgetOrFallback(m_mainWindow->m_planeCombo, m_mainWindow->m_tb1);
        case 4:
            if (phase == 2 && m_mainWindow->m_materialCatalogDialog && m_mainWindow->m_materialCatalogDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_materialCatalogDialog.data();
                if (dlg->m_listWidget && dlg->m_listWidget->isVisible()) {
                    if (QListWidgetItem* item = dlg->m_listWidget->currentItem()) {
                        const QRect ir = dlg->m_listWidget->visualItemRect(item);
                        if (ir.isValid() && !ir.isEmpty() && subRectInTarget) {
                            *subRectInTarget = ir;
                        }
                    }
                    return dlg->m_listWidget;
                }
                return dlg;
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbMaterials), m_mainWindow->m_tb2);
        case 5:
            if (phase == 2 && m_mainWindow->m_sectionCatalogDialog && m_mainWindow->m_sectionCatalogDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_sectionCatalogDialog.data();
                if (dlg->m_listWidget && dlg->m_listWidget->isVisible()) {
                    if (QListWidgetItem* item = dlg->m_listWidget->currentItem()) {
                        const QRect ir = dlg->m_listWidget->visualItemRect(item);
                        if (ir.isValid() && !ir.isEmpty() && subRectInTarget) {
                            *subRectInTarget = ir;
                        }
                    }
                    return dlg->m_listWidget;
                }
                return dlg;
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbSections), m_mainWindow->m_tb2);
        case 6:
            return (phase == 2)
                ? firstVisibleWidgetOrFallback(m_mainWindow->m_btnAddNode, m_mainWindow->m_inspectorDock)
                : firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb1, m_mainWindow->m_actAddNode), m_mainWindow->m_tb1);
        case 7:
            return (phase == 2)
                ? firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb1, m_mainWindow->m_actDelete), m_mainWindow->m_tb1)
                : menuActionTarget(m_mainWindow->m_editMenu);
        case 8:
            return firstVisibleWidgetOrFallback(m_mainWindow->m_planeOffsetSpin, m_mainWindow->m_tb1);
        case 9:
        case 10:
            return (phase == 2)
                ? static_cast<QWidget*>(m_mainWindow->m_viewport)
                : firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb1, m_mainWindow->m_actAddElement), m_mainWindow->m_tb1);
        case 11:
            return (phase == 2)
                ? firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb1, m_mainWindow->m_actLocalAxes), m_mainWindow->m_tb1)
                : firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb1, m_mainWindow->m_actMeasure), m_mainWindow->m_tb1);
        case 12:
            if (phase == 2 && m_mainWindow->m_supportDialog && m_mainWindow->m_supportDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_supportDialog.data();
                return firstVisibleWidgetOrFallback(exNum == 3 ? dlg->m_btnPinned : dlg->m_btnFixed, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbSupports), m_mainWindow->m_tb2);
        case 13:
            if (phase == 2 && m_mainWindow->m_inspGroup) {
                if (auto* cb = m_mainWindow->m_inspGroup->findChild<QComboBox*>(QStringLiteral("insp_sec_combo"))) {
                    if (cb->isVisible()) return cb;
                }
                return m_mainWindow->m_inspGroup;
            }
            return firstVisibleWidgetOrFallback(m_mainWindow->m_inspectorDock, m_mainWindow);
        case 14:
            if (phase == 2 && m_mainWindow->m_releasesDialog && m_mainWindow->m_releasesDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_releasesDialog.data();
                return firstVisibleWidgetOrFallback(exNum == 3 ? dlg->m_btnTruss : dlg->m_btnNone, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbReleases), m_mainWindow->m_tb2);
        case 15:
            if (phase == 2 && m_mainWindow->m_nodalLoadDialog && m_mainWindow->m_nodalLoadDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_nodalLoadDialog.data();
                return firstVisibleWidgetOrFallback(exNum == 1 ? dlg->m_fxSpin : dlg->m_fzSpin, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbNodalLoads), m_mainWindow->m_tb2);
        case 16:
            if (phase == 2 && m_mainWindow->m_distLoadDialog && m_mainWindow->m_distLoadDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_distLoadDialog.data();
                return firstVisibleWidgetOrFallback(dlg->m_qzSpin, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbDistLoads), m_mainWindow->m_tb2);
        case 17:
            if (phase == 2 && m_mainWindow->m_thermalLoadDialog && m_mainWindow->m_thermalLoadDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_thermalLoadDialog.data();
                return firstVisibleWidgetOrFallback(dlg->m_deltaTSpin, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbThermalLoads), m_mainWindow->m_tb2);
        case 18:
            if (phase == 2 && m_mainWindow->m_rollAngleDialog && m_mainWindow->m_rollAngleDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_rollAngleDialog.data();
                return firstVisibleWidgetOrFallback(dlg->m_angleSpin, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbRollAngle), m_mainWindow->m_tb2);
        case 19:
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb1, m_mainWindow->m_actViewIsoTb), m_mainWindow->m_viewport);
        case 20:
            return firstVisibleWidgetOrFallback(m_mainWindow->m_btnSolve, m_mainWindow->m_tb2);
        case 21:
            return (phase == 2)
                ? firstVisibleWidgetOrFallback(m_mainWindow->m_scaleSlider, m_mainWindow->m_diagramCombo)
                : firstVisibleWidgetOrFallback(m_mainWindow->m_diagramCombo, m_mainWindow->m_tb2);
        case 22:
            return firstVisibleWidgetOrFallback(m_mainWindow->m_resultsTabs, m_mainWindow->m_resultsDock);
        case 23:
            if (phase == 2 && m_mainWindow->m_loadCombinationsDialog && m_mainWindow->m_loadCombinationsDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_loadCombinationsDialog.data();
                return firstVisibleWidgetOrFallback(dlg->m_tabs ? static_cast<QWidget*>(dlg->m_tabs->tabBar()) : nullptr, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbCombos), m_mainWindow->m_tb2);
        case 24:
            if (phase == 2 && m_mainWindow->m_meshGeneratorDialog && m_mainWindow->m_meshGeneratorDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_meshGeneratorDialog.data();
                return firstVisibleWidgetOrFallback(dlg->m_btnGenerate, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbMesh), m_mainWindow->m_tb2);
        case 25:
            if (phase == 2 && m_mainWindow->m_analysisSuiteDialog && m_mainWindow->m_analysisSuiteDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_analysisSuiteDialog.data();
                return firstVisibleWidgetOrFallback(dlg->m_tabs ? static_cast<QWidget*>(dlg->m_tabs->tabBar()) : nullptr, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbAdvAnalysis), m_mainWindow->m_tb2);
        case 26:
        case 27:
            if (m_mainWindow->m_analysisSuiteDialog && m_mainWindow->m_analysisSuiteDialog->isVisible()) {
                auto* dlg = m_mainWindow->m_analysisSuiteDialog.data();
                return firstVisibleWidgetOrFallback(dlg->m_tabs ? static_cast<QWidget*>(dlg->m_tabs->tabBar()) : nullptr, dlg);
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbAdvAnalysis), m_mainWindow->m_tb2);
        case 28:
            if (phase == 2 && m_mainWindow->m_postProcessControlDialog && m_mainWindow->m_postProcessControlDialog->isVisible()) {
                return firstVisibleWidgetOrFallback(m_mainWindow->m_postProcessControlDialog->m_contourCombo,
                                                    m_mainWindow->m_postProcessControlDialog.data());
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbPostCtrl), m_mainWindow->m_tb2);
        case 29:
            if (phase == 2 && m_mainWindow->m_nbrComplianceDialog && m_mainWindow->m_nbrComplianceDialog->isVisible()) {
                return firstVisibleWidgetOrFallback(m_mainWindow->m_nbrComplianceDialog->m_btnApplyDcrViewport,
                                                    m_mainWindow->m_nbrComplianceDialog.data());
            }
            return firstVisibleWidgetOrFallback(tbWidget(m_mainWindow->m_tb2, m_mainWindow->m_actTbNbrAudit), m_mainWindow->m_tb2);
        case 30:
        default:
            return menuActionTarget(m_mainWindow->m_fileMenu);
    }
}

void GuidedSessionDialog::positionChildDialogForGuidedSession(QWidget* childDlg) const {
    if (!m_mainWindow || !childDlg) return;
    const QPoint mainTopLeft = m_mainWindow->mapToGlobal(QPoint(0, 0));
    const QRect mainClient(mainTopLeft, m_mainWindow->size());
    const int hudW = isVisible() ? frameGeometry().width() : width();
    const int maxChildW = std::clamp(mainClient.width() - hudW - 36, 420, std::max(420, mainClient.width() - 32));
    const int maxChildH = std::clamp(mainClient.height() - 240, 320, std::max(320, mainClient.height() - 36));
    const int newW = std::min(childDlg->width(), maxChildW);
    const int newH = std::min(childDlg->height(), maxChildH);
    if (newW != childDlg->width() || newH != childDlg->height()) {
        childDlg->resize(newW, newH);
    }

    const QScreen* screen = m_mainWindow->screen() ? m_mainWindow->screen() : QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : mainClient;
    const int frameExtraH = std::max(0, childDlg->frameGeometry().height() - childDlg->height());
    int targetX = mainClient.left() + 14;
    int targetY = mainClient.bottom() - childDlg->height() - frameExtraH - 14;
    targetX = std::clamp(targetX, avail.left() + 8, std::max(avail.left() + 8, avail.right() - childDlg->frameGeometry().width() - 8));
    targetY = std::clamp(targetY, avail.top() + 8, std::max(avail.top() + 8, avail.bottom() - childDlg->frameGeometry().height() - 8));
    childDlg->move(targetX, targetY);
}

void GuidedSessionDialog::updateVisualHighlight() {
    using locale::GerenciadorTraducao;
    if (!m_mainWindow || !m_running || m_currentStep <= 0) {
        clearVisualHighlight();
        return;
    }

    QRect subRect;
    QPoint customPt;
    QWidget* target = resolveTargetWidgetForStep(m_currentStep, m_currentPhase, &subRect, &customPt);
    if (!target) {
        clearVisualHighlight();
        return;
    }

    QWidget* hostWin = target->window();
    if (!hostWin || !hostWin->isVisible()) {
        hostWin = m_mainWindow;
        target = m_mainWindow->m_viewport ? static_cast<QWidget*>(m_mainWindow->m_viewport) : static_cast<QWidget*>(m_mainWindow);
        subRect = QRect();
    }

    m_currentHighlightedWidget = target;
    m_currentHighlightRect = subRect.isValid() ? subRect : target->rect();

    if (!m_overlay) {
        m_overlay = new GuidedTargetOverlay(nullptr);
        m_overlay->setObjectName(QStringLiteral("guided_target_overlay"));
    }

    const QString badgeTxt = (m_currentPhase == 1)
        ? GerenciadorTraducao::trText("FASE 1/2 — PRÓXIMO CONTROLE A ACIONAR")
        : GerenciadorTraducao::trText("FASE 2/2 — EXECUTANDO NESTE CONTROLE");

    m_overlay->setShowCalloutCard(true);

    if (hostWin != m_mainWindow) {
        m_overlay->setShowSpotlight(false);
        m_overlay->setHighlight(m_mainWindow, target, subRect, m_currentPhase, badgeTxt, m_currentUnifiedHtml);

        if (!m_childOverlay) {
            m_childOverlay = new GuidedTargetOverlay(nullptr);
            m_childOverlay->setObjectName(QStringLiteral("guided_child_target_overlay"));
        }
        m_childOverlay->setShowCalloutCard(false);
        m_childOverlay->setShowSpotlight(true);
        m_childOverlay->setHighlight(hostWin, target, subRect, m_currentPhase, badgeTxt, QString());
        if (m_childOverlay->isVisible()) {
            m_currentHighlightRect = m_childOverlay->targetRectInHost();
        }
    } else {
        if (m_childOverlay) {
            m_childOverlay->clearHighlight();
        }
        m_overlay->setShowSpotlight(true);
        m_overlay->setHighlight(m_mainWindow, target, subRect, m_currentPhase, badgeTxt, m_currentUnifiedHtml);
        if (m_overlay->isVisible()) {
            m_currentHighlightRect = m_overlay->targetRectInHost();
        }
    }
}

void GuidedSessionDialog::clearVisualHighlight() {
    m_currentHighlightedWidget.clear();
    m_currentHighlightRect = QRect();
    if (m_overlay) {
        m_overlay->clearHighlight();
    }
    if (m_childOverlay) {
        m_childOverlay->clearHighlight();
    }
}

void GuidedSessionDialog::showPreActionTooltipForStep(int step) {
    if (!m_mainWindow) return;
    Q_UNUSED(step);
    refreshCurrentStepTexts();
    QToolTip::hideText();
    m_mainWindow->showStatusMessage(m_currentStepTitle);
    updateVisualHighlight();
}

void GuidedSessionDialog::closeAuxiliaryInputDialogs() {
    if (!m_mainWindow) return;
    clearVisualHighlight();
    auto safeClose = [](QDialog* d) {
        if (d && d->isVisible()) {
            d->close();
        }
    };
    safeClose(m_mainWindow->m_materialCatalogDialog.data());
    safeClose(m_mainWindow->m_sectionCatalogDialog.data());
    safeClose(m_mainWindow->m_supportDialog.data());
    safeClose(m_mainWindow->m_releasesDialog.data());
    safeClose(m_mainWindow->m_nodalLoadDialog.data());
    safeClose(m_mainWindow->m_distLoadDialog.data());
    safeClose(m_mainWindow->m_thermalLoadDialog.data());
    safeClose(m_mainWindow->m_rollAngleDialog.data());
    safeClose(m_mainWindow->m_loadCombinationsDialog.data());
    safeClose(m_mainWindow->m_meshGeneratorDialog.data());
    if (m_mainWindow->m_analysisSuiteDialog && m_mainWindow->m_analysisSuiteDialog->isVisible()) {
        m_mainWindow->m_analysisSuiteDialog->hide();
    }
    if (m_mainWindow->m_postProcessControlDialog && m_mainWindow->m_postProcessControlDialog->isVisible()) {
        m_mainWindow->m_postProcessControlDialog->hide();
    }
    if (m_mainWindow->m_nbrComplianceDialog && m_mainWindow->m_nbrComplianceDialog->isVisible()) {
        m_mainWindow->m_nbrComplianceDialog->hide();
    }
}

void GuidedSessionDialog::pumpUiEvents(int ms) {
    QElapsedTimer t;
    t.start();
    do {
        QApplication::processEvents(QEventLoop::AllEvents, 15);
        if (ms > 0) {
            QThread::msleep(5);
        }
    } while (t.elapsed() < ms);
}

bool GuidedSessionDialog::waitForBackgroundSolvers(int timeoutMs) {
    if (!m_mainWindow) return false;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() <= timeoutMs) {
        const bool mainBusy = m_mainWindow->m_solveInProgress;
        const bool suiteBusy = m_mainWindow->m_analysisSuiteDialog && m_mainWindow->m_analysisSuiteDialog->isBusy();
        if (!mainBusy && !suiteBusy) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            return true;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return false;
}

void GuidedSessionDialog::commitPendingStepEdits(int completedStep) {
    if (!m_mainWindow || !m_mainWindow->m_viewport) return;
    StructuralModel& model = m_mainWindow->m_model;
    const int exNum = selectedExampleNumber();

    switch (completedStep) {
        case 4:
            if (m_mainWindow->m_materialCatalogDialog && m_mainWindow->m_materialCatalogDialog->isVisible()) {
                m_mainWindow->m_materialCatalogDialog->close();
            }
            break;
        case 5:
            if (m_mainWindow->m_sectionCatalogDialog && m_mainWindow->m_sectionCatalogDialog->isVisible()) {
                m_mainWindow->m_sectionCatalogDialog->close();
            }
            break;
        case 12:
            if (m_mainWindow->m_supportDialog && m_mainWindow->m_supportDialog->isVisible()) {
                m_mainWindow->m_supportDialog->accept();
            }
            if (exNum == 1) {
                for (NodeId nid : {1, 2, 3, 4}) {
                    if (Node* n = model.findNode(nid)) n->support = SupportRestraint::makeFixed();
                }
            } else if (exNum == 2) {
                if (Node* n = model.findNode(1)) n->support = SupportRestraint::makeFixed();
            } else {
                if (Node* n1 = model.findNode(1)) n1->support = SupportRestraint::makePinned();
                if (Node* n2 = model.findNode(2)) {
                    n2->support = SupportRestraint::makeRoller(2);
                    n2->support.fixed[0] = true;
                }
                if (Node* n9 = model.findNode(9)) n9->support = SupportRestraint::makeRoller(2);
                if (Node* n10 = model.findNode(10)) n10->support = SupportRestraint::makeRoller(2);
            }
            model.pushUndo();
            m_mainWindow->onModelModified();
            break;
        case 14:
            if (m_mainWindow->m_releasesDialog && m_mainWindow->m_releasesDialog->isVisible()) {
                m_mainWindow->m_releasesDialog->accept();
            }
            if (exNum == 3) {
                const ElementReleases trussRel = ElementReleases::makeTrussReleases();
                for (auto& el : model.getElements()) {
                    el.releases = trussRel;
                }
                model.pushUndo();
                m_mainWindow->onModelModified();
            }
            break;
        case 15:
            if (m_mainWindow->m_nodalLoadDialog && m_mainWindow->m_nodalLoadDialog->isVisible()) {
                m_mainWindow->m_nodalLoadDialog->accept();
            }
            if (exNum == 1) {
                if (Node* n5 = model.findNode(5)) n5->load.force = {20.0, 0.0, 0.0};
                if (Node* n8 = model.findNode(8)) n8->load.force = {20.0, 0.0, 0.0};
                if (Node* n9 = model.findNode(9)) n9->load.force = {30.0, 0.0, 0.0};
                if (Node* n12 = model.findNode(12)) n12->load.force = {30.0, 0.0, 0.0};
            } else if (exNum == 2) {
                if (Node* n3 = model.findNode(3)) n3->load.force = {0.0, 5.0, -15.0};
            } else {
                for (NodeId nid : {3, 4, 5, 6, 7, 8}) {
                    if (Node* n = model.findNode(nid)) n->load.force = {0.0, 0.0, -20.0};
                }
            }
            model.pushUndo();
            m_mainWindow->onModelModified();
            break;
        case 16:
            if (m_mainWindow->m_distLoadDialog && m_mainWindow->m_distLoadDialog->isVisible()) {
                if (exNum == 1) {
                    m_mainWindow->m_distLoadDialog->accept();
                } else {
                    m_mainWindow->m_distLoadDialog->reject();
                }
            }
            if (exNum == 1) {
                DistributedLoad qBeam;
                qBeam.active = true;
                qBeam.coordSystem = CoordSystem::Global;
                qBeam.w = {0.0, 0.0, -15.0};

                DistributedLoad qRoof;
                qRoof.active = true;
                qRoof.coordSystem = CoordSystem::Global;
                qRoof.w = {0.0, 0.0, -10.0};

                for (ElementId eid : {9, 10, 11, 12}) {
                    if (Element* el = model.findElement(eid)) el->distLoad = qBeam;
                }
                for (ElementId eid : {13, 14, 15, 16}) {
                    if (Element* el = model.findElement(eid)) el->distLoad = qRoof;
                }
                model.pushUndo();
                m_mainWindow->onModelModified();
            }
            break;
        case 17:
            if (m_mainWindow->m_thermalLoadDialog && m_mainWindow->m_thermalLoadDialog->isVisible()) {
                m_mainWindow->m_thermalLoadDialog->reject();
            }
            break;
        case 18:
            if (m_mainWindow->m_rollAngleDialog && m_mainWindow->m_rollAngleDialog->isVisible()) {
                m_mainWindow->m_rollAngleDialog->reject();
            }
            break;
        case 23:
            if (m_mainWindow->m_loadCombinationsDialog && m_mainWindow->m_loadCombinationsDialog->isVisible()) {
                m_mainWindow->m_loadCombinationsDialog->close();
            }
            break;
        case 24:
            if (m_mainWindow->m_meshGeneratorDialog && m_mainWindow->m_meshGeneratorDialog->isVisible()) {
                m_mainWindow->m_meshGeneratorDialog->close();
            }
            break;
        case 27:
            if (m_mainWindow->m_analysisSuiteDialog && m_mainWindow->m_analysisSuiteDialog->isVisible()) {
                m_mainWindow->m_analysisSuiteDialog->hide();
            }
            break;
        case 28:
            if (auto* dlg = m_mainWindow->m_postProcessControlDialog.data()) {
                if (dlg->m_btnPlayPause && dlg->m_btnPlayPause->isChecked()) {
                    dlg->m_btnPlayPause->click();
                }
                if (dlg->m_clipAxisCombo) {
                    const int noneIdx = dlg->m_clipAxisCombo->findData(static_cast<int>(Viewport3D::ClipAxis::None));
                    if (noneIdx >= 0) dlg->m_clipAxisCombo->setCurrentIndex(noneIdx);
                }
                if (dlg->isVisible()) dlg->hide();
            }
            break;
        case 29:
            if (m_mainWindow->m_nbrComplianceDialog && m_mainWindow->m_nbrComplianceDialog->isVisible()) {
                m_mainWindow->m_nbrComplianceDialog->hide();
            }
            break;
        default:
            break;
    }
    m_activeDialogStep = 0;
}

void GuidedSessionDialog::executeStepAction(int step) {
    if (!m_mainWindow || !m_mainWindow->m_viewport) return;
    StructuralModel& model = m_mainWindow->m_model;
    Viewport3D* viewport = m_mainWindow->m_viewport;
    const int exNum = selectedExampleNumber();

    auto addNodeViaQuickPanel = [this](double x, double y, double z) {
        if (m_mainWindow->m_quickXSpin) m_mainWindow->m_quickXSpin->setValue(x);
        if (m_mainWindow->m_quickYSpin) m_mainWindow->m_quickYSpin->setValue(y);
        if (m_mainWindow->m_quickZSpin) m_mainWindow->m_quickZSpin->setValue(z);
        if (m_mainWindow->m_btnAddNode) m_mainWindow->m_btnAddNode->click();
    };

    auto resolveExMaterialId = [&]() -> MaterialId {
        return (exNum == 1)
            ? findMaterialIdByKeywords(model, {QStringLiteral("C30"), QStringLiteral("Concreto")})
            : findMaterialIdByKeywords(model, {QStringLiteral("A36"), QStringLiteral("Aço")});
    };

    switch (step) {
        case 1: {
            closeAuxiliaryInputDialogs();
            m_mainWindow->m_hasSolvedContext = false;
            m_mainWindow->m_lastSolverResult = SolverResult();
            model.clearAll();
            model.populateStandardCatalog();
            if (exNum == 1) {
                model.metadata().projectName = "Exemplo 1: Pórtico Espacial de Edifício (Reconstruído do Zero)";
            } else if (exNum == 2) {
                model.metadata().projectName = "Exemplo 2: Balanço Espacial em L (Reconstruído do Zero)";
            } else {
                model.metadata().projectName = "Exemplo 3: Treliça Espacial Passarela 3D (Reconstruída do Zero)";
            }
            model.clearUndoHistory();
            model.pushUndo();
            m_mainWindow->onModelModified();
            viewport->fitAll();
            break;
        }
        case 2: {
            if (m_mainWindow->m_actPresetHigh) m_mainWindow->m_actPresetHigh->trigger();
            if (m_mainWindow->m_actShaderLineAa) m_mainWindow->m_actShaderLineAa->setChecked(true);
            if (m_mainWindow->m_actPlaneSurface) m_mainWindow->m_actPlaneSurface->setChecked(true);
            if (m_mainWindow->m_actResolutionHud) m_mainWindow->m_actResolutionHud->setChecked(true);
            break;
        }
        case 3: {
            if (m_mainWindow->m_planeCombo) {
                const int xyIdx = m_mainWindow->m_planeCombo->findData(static_cast<int>(WorkPlane::XY_Top));
                if (xyIdx >= 0) m_mainWindow->m_planeCombo->setCurrentIndex(xyIdx);
            }
            if (m_mainWindow->m_planeOffsetSpin) m_mainWindow->m_planeOffsetSpin->setValue(0.0);
            if (m_mainWindow->m_gridStepSpin) m_mainWindow->m_gridStepSpin->setValue(exNum == 3 ? 0.50 : 1.00);
            if (m_mainWindow->m_actSnap) m_mainWindow->m_actSnap->setChecked(true);
            if (m_mainWindow->m_actViewIsoTb) m_mainWindow->m_actViewIsoTb->trigger();
            break;
        }
        case 4: {
            m_mainWindow->onAssignMaterials();
            m_activeDialogStep = 4;
            if (auto* dlg = m_mainWindow->m_materialCatalogDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                selectListRowByKeywords(
                    dlg->m_listWidget,
                    (exNum == 1)
                        ? QStringList{QStringLiteral("C30"), QStringLiteral("Concreto")}
                        : QStringList{QStringLiteral("A36"), QStringLiteral("Aço")});
            }
            break;
        }
        case 5: {
            m_mainWindow->onAssignSections();
            m_activeDialogStep = 5;
            if (auto* dlg = m_mainWindow->m_sectionCatalogDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                QStringList kw;
                if (exNum == 1) {
                    kw = {QStringLiteral("Pilar 30x30"), QStringLiteral("30x30")};
                } else if (exNum == 2) {
                    kw = {QStringLiteral("Tubo Circular D200"), QStringLiteral("D200")};
                } else {
                    kw = {QStringLiteral("Tubo Treliça D100"), QStringLiteral("D100")};
                }
                selectListRowByKeywords(dlg->m_listWidget, kw);
            }
            break;
        }
        case 6: {
            m_mainWindow->setInspectorDockCollapsed(false);
            if (m_mainWindow->m_inspectorDock) {
                m_mainWindow->m_inspectorDock->show();
                m_mainWindow->m_inspectorDock->raise();
            }
            if (m_mainWindow->m_actAddNode) m_mainWindow->m_actAddNode->setChecked(true);

            if (exNum == 1) {
                addNodeViaQuickPanel(0.0, 0.0, 0.0);
                addNodeViaQuickPanel(6.0, 0.0, 0.0);
                addNodeViaQuickPanel(6.0, 5.0, 0.0);
                addNodeViaQuickPanel(0.0, 5.0, 0.0);
            } else if (exNum == 2) {
                addNodeViaQuickPanel(0.0, 0.0, 0.0);
                addNodeViaQuickPanel(4.0, 0.0, 0.0);
            } else {
                addNodeViaQuickPanel(0.0, 0.0, 0.0);
                addNodeViaQuickPanel(0.0, 2.0, 0.0);
                addNodeViaQuickPanel(2.5, 0.0, 0.0);
                addNodeViaQuickPanel(2.5, 2.0, 0.0);
            }
            viewport->fitAll();
            break;
        }
        case 7: {
            addNodeViaQuickPanel(9.0, 9.0, 0.0);
            if (m_mainWindow->m_actDelete) m_mainWindow->m_actDelete->setChecked(true);
            if (m_mainWindow->m_actUndo) m_mainWindow->m_actUndo->trigger();
            if (m_mainWindow->m_actRedo) m_mainWindow->m_actRedo->trigger();
            if (m_mainWindow->m_actUndo) m_mainWindow->m_actUndo->trigger();
            if (m_mainWindow->m_actAddNode) m_mainWindow->m_actAddNode->setChecked(true);
            break;
        }
        case 8: {
            if (exNum == 1) {
                if (m_mainWindow->m_planeOffsetSpin) m_mainWindow->m_planeOffsetSpin->setValue(3.0);
                addNodeViaQuickPanel(0.0, 0.0, 3.0);
                addNodeViaQuickPanel(6.0, 0.0, 3.0);
                addNodeViaQuickPanel(6.0, 5.0, 3.0);
                addNodeViaQuickPanel(0.0, 5.0, 3.0);

                if (m_mainWindow->m_planeOffsetSpin) m_mainWindow->m_planeOffsetSpin->setValue(6.0);
                addNodeViaQuickPanel(0.0, 0.0, 6.0);
                addNodeViaQuickPanel(6.0, 0.0, 6.0);
                addNodeViaQuickPanel(6.0, 5.0, 6.0);
                addNodeViaQuickPanel(0.0, 5.0, 6.0);
            } else if (exNum == 2) {
                addNodeViaQuickPanel(4.0, 3.0, 0.0);
            } else {
                const double bayLen = 2.5;
                const double width = 2.0;
                const double height = 2.0;
                for (int i = 2; i <= 4; ++i) {
                    const double x = i * bayLen;
                    addNodeViaQuickPanel(x, 0.0, 0.0);
                    addNodeViaQuickPanel(x, width, 0.0);
                }
                if (m_mainWindow->m_planeOffsetSpin) m_mainWindow->m_planeOffsetSpin->setValue(height);
                for (int i = 0; i <= 4; ++i) {
                    const double x = i * bayLen;
                    addNodeViaQuickPanel(x, 0.0, height);
                    addNodeViaQuickPanel(x, width, height);
                }
            }
            if (m_mainWindow->m_planeOffsetSpin) m_mainWindow->m_planeOffsetSpin->setValue(0.0);
            viewport->fitAll();
            break;
        }
        case 9: {
            if (m_mainWindow->m_actAddElement) m_mainWindow->m_actAddElement->setChecked(true);
            const MaterialId matId = resolveExMaterialId();

            if (exNum == 1) {
                const SectionId secCol = findSectionIdByKeywords(model, {QStringLiteral("Pilar 30x30"), QStringLiteral("30x30")});
                model.addElement(1, 5, matId, secCol);
                model.addElement(2, 6, matId, secCol);
                model.addElement(3, 7, matId, secCol);
                model.addElement(4, 8, matId, secCol);
                model.addElement(5, 9, matId, secCol);
                model.addElement(6, 10, matId, secCol);
                model.addElement(7, 11, matId, secCol);
                model.addElement(8, 12, matId, secCol);
            } else if (exNum == 2) {
                const SectionId secTube = findSectionIdByKeywords(model, {QStringLiteral("Tubo Circular D200"), QStringLiteral("D200")});
                model.addElement(1, 2, matId, secTube);
            } else {
                const SectionId secTruss = findSectionIdByKeywords(model, {QStringLiteral("Tubo Treliça D100"), QStringLiteral("D100")});
                addTrussChordBars(model, matId, secTruss);
            }
            m_mainWindow->onModelModified();
            viewport->fitAll();
            break;
        }
        case 10: {
            if (m_mainWindow->m_actAddElement) m_mainWindow->m_actAddElement->setChecked(true);
            const MaterialId matId = resolveExMaterialId();

            if (exNum == 1) {
                const SectionId secBeam = findSectionIdByKeywords(model, {QStringLiteral("Viga 20x50"), QStringLiteral("20x50")});
                model.addElement(5, 6, matId, secBeam);
                model.addElement(6, 7, matId, secBeam);
                model.addElement(7, 8, matId, secBeam);
                model.addElement(8, 5, matId, secBeam);
                model.addElement(9, 10, matId, secBeam);
                model.addElement(10, 11, matId, secBeam);
                model.addElement(11, 12, matId, secBeam);
                model.addElement(12, 9, matId, secBeam);
            } else if (exNum == 2) {
                const SectionId secTube = findSectionIdByKeywords(model, {QStringLiteral("Tubo Circular D200"), QStringLiteral("D200")});
                model.addElement(2, 3, matId, secTube);
            } else {
                const SectionId secTruss = findSectionIdByKeywords(model, {QStringLiteral("Tubo Treliça D100"), QStringLiteral("D100")});
                addTrussWebAndBracingBars(model, matId, secTruss);
            }
            m_mainWindow->onModelModified();
            viewport->fitAll();
            break;
        }
        case 11: {
            if (m_mainWindow->m_actLocalAxes) m_mainWindow->m_actLocalAxes->setChecked(true);
            if (m_mainWindow->m_actMeasure) m_mainWindow->m_actMeasure->setChecked(true);
            if (m_mainWindow->m_actSelect) m_mainWindow->m_actSelect->setChecked(true);
            viewport->update();
            break;
        }
        case 12: {
            model.selectNode(1, true);
            m_mainWindow->onSelectionChanged();
            m_mainWindow->onAssignSupports();
            m_activeDialogStep = 12;
            if (auto* dlg = m_mainWindow->m_supportDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                if (exNum == 3) {
                    if (dlg->m_btnPinned) dlg->m_btnPinned->click();
                } else {
                    if (dlg->m_btnFixed) dlg->m_btnFixed->click();
                }
            }
            break;
        }
        case 13: {
            m_mainWindow->setInspectorDockCollapsed(false);
            if (m_mainWindow->m_inspectorDock) {
                m_mainWindow->m_inspectorDock->show();
                m_mainWindow->m_inspectorDock->raise();
            }
            if (!model.getElements().empty()) {
                const ElementId eid = model.getElements().front().id;
                model.selectElement(eid, true);
                m_mainWindow->onSelectionChanged();
                pumpUiEvents(25);
                if (m_mainWindow->m_inspGroup) {
                    if (auto* secCombo = m_mainWindow->m_inspGroup->findChild<QComboBox*>(QStringLiteral("insp_sec_combo"))) {
                        const int cur = secCombo->currentIndex();
                        if (secCombo->count() > 1) {
                            const int alt = (cur + 1) % secCombo->count();
                            secCombo->setCurrentIndex(alt);
                            emit secCombo->activated(alt);
                            secCombo->setCurrentIndex(cur);
                            emit secCombo->activated(cur);
                        }
                    }
                }
            }
            if (!model.getNodes().empty()) {
                model.selectNode(model.getNodes().back().id, true);
                m_mainWindow->onSelectionChanged();
            }
            if (m_mainWindow->m_inspectorDock) {
                if (auto* scroll = m_mainWindow->m_inspectorDock->findChild<QScrollArea*>()) {
                    if (auto* sb = scroll->verticalScrollBar()) {
                        sb->setValue(sb->maximum());
                        sb->setValue(sb->minimum());
                    }
                }
            }
            break;
        }
        case 14: {
            if (!model.getElements().empty()) {
                model.selectElement(model.getElements().front().id, true);
                m_mainWindow->onSelectionChanged();
                m_mainWindow->onAssignReleases();
                m_activeDialogStep = 14;
                if (auto* dlg = m_mainWindow->m_releasesDialog.data()) {
                    positionChildDialogForGuidedSession(dlg);
                    if (exNum == 3) {
                        if (dlg->m_btnTruss) dlg->m_btnTruss->click();
                    } else {
                        if (dlg->m_btnNone) dlg->m_btnNone->click();
                    }
                }
            }
            break;
        }
        case 15: {
            NodeId targetNode = 1;
            NodalLoad previewLoad;
            if (exNum == 1) {
                targetNode = 9;
                previewLoad.force = {30.0, 0.0, 0.0};
            } else if (exNum == 2) {
                targetNode = 3;
                previewLoad.force = {0.0, 5.0, -15.0};
            } else {
                targetNode = 3;
                previewLoad.force = {0.0, 0.0, -20.0};
            }
            model.selectNode(targetNode, true);
            m_mainWindow->onSelectionChanged();
            m_mainWindow->onAssignNodalLoads();
            m_activeDialogStep = 15;
            if (auto* dlg = m_mainWindow->m_nodalLoadDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                dlg->setLoad(previewLoad);
            }
            break;
        }
        case 16: {
            const ElementId targetElem = (exNum == 1 && model.findElement(9)) ? 9 : model.getElements().back().id;
            model.selectElement(targetElem, true);
            m_mainWindow->onSelectionChanged();
            m_mainWindow->onAssignDistributedLoads();
            m_activeDialogStep = 16;
            if (auto* dlg = m_mainWindow->m_distLoadDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                DistributedLoad dl;
                dl.active = true;
                dl.coordSystem = CoordSystem::Global;
                dl.w = {0.0, 0.0, (exNum == 1) ? -15.0 : -2.5};
                dlg->setLoad(dl);
            }
            break;
        }
        case 17: {
            const ElementId targetElem = model.getElements().front().id;
            model.selectElement(targetElem, true);
            m_mainWindow->onSelectionChanged();
            m_mainWindow->onAssignThermalLoads();
            m_activeDialogStep = 17;
            if (auto* dlg = m_mainWindow->m_thermalLoadDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                ThermalLoad th;
                th.active = true;
                th.deltaT = 15.0;
                dlg->setThermalLoad(th);
            }
            break;
        }
        case 18: {
            const ElementId targetElem = model.getElements().front().id;
            model.selectElement(targetElem, true);
            m_mainWindow->onSelectionChanged();
            m_mainWindow->onAssignRollAngle();
            m_activeDialogStep = 18;
            if (auto* dlg = m_mainWindow->m_rollAngleDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                dlg->setAngleDegrees(30.0);
            }
            break;
        }
        case 19: {
            if (m_mainWindow->m_actViewFrontMenu) m_mainWindow->m_actViewFrontMenu->trigger();
            if (m_mainWindow->m_actViewTopMenu) m_mainWindow->m_actViewTopMenu->trigger();
            if (m_mainWindow->m_actViewSideMenu) m_mainWindow->m_actViewSideMenu->trigger();
            if (m_mainWindow->m_actViewIsoMenu) m_mainWindow->m_actViewIsoMenu->trigger();
            if (m_mainWindow->m_actViewFitMenu) m_mainWindow->m_actViewFitMenu->trigger();
            break;
        }
        case 20: {
            if (m_mainWindow->m_actAutoRecalc && !m_mainWindow->m_actAutoRecalc->isChecked()) {
                m_mainWindow->m_actAutoRecalc->setChecked(true);
            }
            if (m_mainWindow->m_btnSolve) {
                m_mainWindow->m_btnSolve->click();
            } else {
                m_mainWindow->onSolveStructure();
            }
            waitForBackgroundSolvers();
            if (!m_mainWindow->m_lastSolverResult.success) {
                m_mainWindow->ensureStaticSolvedSync();
            }
            break;
        }
        case 21: {
            if (m_mainWindow->m_actReactions) m_mainWindow->m_actReactions->setChecked(true);
            if (m_mainWindow->m_scaleSlider) m_mainWindow->m_scaleSlider->setValue(55);
            if (m_mainWindow->m_diagramCombo) {
                const int defIdx = m_mainWindow->m_diagramCombo->findData(static_cast<int>(DiagramType::Deformed));
                const int nIdx = m_mainWindow->m_diagramCombo->findData(static_cast<int>(DiagramType::Normal_N));
                const int mzIdx = m_mainWindow->m_diagramCombo->findData(static_cast<int>(DiagramType::Moment_Mz));
                if (defIdx >= 0) m_mainWindow->m_diagramCombo->setCurrentIndex(defIdx);
                if (nIdx >= 0) m_mainWindow->m_diagramCombo->setCurrentIndex(nIdx);
                if (mzIdx >= 0 && exNum != 3) m_mainWindow->m_diagramCombo->setCurrentIndex(mzIdx);
            }
            break;
        }
        case 22: {
            m_mainWindow->setResultsDockCollapsed(false);
            if (m_mainWindow->m_resultsDock) {
                m_mainWindow->m_resultsDock->show();
                m_mainWindow->m_resultsDock->raise();
            }
            if (m_mainWindow->m_resultsTabs) {
                m_mainWindow->m_resultsTabs->setCurrentIndex(0);
                scrollTableToBottomAndTop(m_mainWindow->m_dispTable);
                m_mainWindow->m_resultsTabs->setCurrentIndex(1);
                scrollTableToBottomAndTop(m_mainWindow->m_reacTable);
                m_mainWindow->m_resultsTabs->setCurrentIndex(2);
                scrollTableToBottomAndTop(m_mainWindow->m_forcesTable);
                if (m_mainWindow->m_resultsTabs->count() >= 4 && m_mainWindow->m_continuumTable) {
                    m_mainWindow->m_resultsTabs->setCurrentIndex(3);
                    scrollTableToBottomAndTop(m_mainWindow->m_continuumTable);
                }
            }
            break;
        }
        case 23: {
            m_mainWindow->onOpenLoadCombinations();
            m_activeDialogStep = 23;
            if (auto* dlg = m_mainWindow->m_loadCombinationsDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                if (dlg->m_autoMaterialCombo) {
                    dlg->m_autoMaterialCombo->setCurrentIndex(exNum == 1 ? 0 : 1);
                }
                if (dlg->m_tabs) dlg->m_tabs->setCurrentIndex(1);
                dlg->onGenerateNbrCombos();
                if (dlg->m_tabs) dlg->m_tabs->setCurrentIndex(2);
                dlg->onComputeWind6123();
                dlg->onComputePartition6120();
            }
            waitForBackgroundSolvers();
            break;
        }
        case 24: {
            m_mainWindow->onOpenMeshGenerator();
            m_activeDialogStep = 24;
            if (auto* dlg = m_mainWindow->m_meshGeneratorDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                dlg->onFitToModelBounds();
                if (dlg->m_meshTypeCombo && dlg->m_meshTypeCombo->count() > 1) {
                    dlg->m_meshTypeCombo->setCurrentIndex(1);
                }
                if (dlg->m_matCombo && dlg->m_matCombo->count() > 0) {
                    dlg->m_matCombo->setCurrentIndex(0);
                }
                if (dlg->m_thicknessSpin) {
                    dlg->m_thicknessSpin->setValue(0.20);
                }
                if (dlg->m_bcPresetCombo && dlg->m_bcPresetCombo->count() > 1) {
                    dlg->m_bcPresetCombo->setCurrentIndex(1);
                }
                if (dlg->m_pressureSpin) {
                    dlg->m_pressureSpin->setValue(-5.0);
                }
                dlg->onGenerateMesh();
            }
            waitForBackgroundSolvers();
            m_mainWindow->ensureStaticSolvedSync();
            m_mainWindow->setResultsDockCollapsed(false);
            if (m_mainWindow->m_resultsDock) {
                m_mainWindow->m_resultsDock->show();
                m_mainWindow->m_resultsDock->raise();
            }
            m_mainWindow->updateResultsTables();
            if (m_mainWindow->m_resultsTabs && m_mainWindow->m_resultsTabs->count() >= 4 && m_mainWindow->m_continuumTable) {
                m_mainWindow->m_resultsTabs->setCurrentIndex(3);
                scrollTableToBottomAndTop(m_mainWindow->m_continuumTable);
            }
            break;
        }
        case 25: {
            m_mainWindow->onOpenAnalysisSuite();
            m_activeDialogStep = 25;
            if (auto* dlg = m_mainWindow->m_analysisSuiteDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                if (dlg->m_tabs) dlg->m_tabs->setCurrentIndex(0);
                if (dlg->m_linearSolverCombo) {
                    const int ldltIdx = dlg->m_linearSolverCombo->findData(static_cast<int>(kernel::LinearSolverType::SimplicialLDLT));
                    if (ldltIdx >= 0) dlg->m_linearSolverCombo->setCurrentIndex(ldltIdx);
                }
                dlg->onRunCombosAsync();
                waitForBackgroundSolvers();
                dlg->onPlotSelectedCombo();

                if (dlg->m_tabs) dlg->m_tabs->setCurrentIndex(1);
                dlg->onRunModalAsync();
                waitForBackgroundSolvers();
                dlg->onPlotSelectedMode();

                if (dlg->m_tabs) dlg->m_tabs->setCurrentIndex(2);
                dlg->onRunBucklingAsync();
                waitForBackgroundSolvers();
                dlg->onPlotSelectedBucklingMode();
            }
            break;
        }
        case 26: {
            if (!m_mainWindow->m_analysisSuiteDialog || !m_mainWindow->m_analysisSuiteDialog->isVisible()) {
                m_mainWindow->onOpenAnalysisSuite();
            }
            m_activeDialogStep = 26;
            if (auto* dlg = m_mainWindow->m_analysisSuiteDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                if (dlg->m_tabs) dlg->m_tabs->setCurrentIndex(3);
                if (dlg->m_harmStepsSpin) dlg->m_harmStepsSpin->setValue(10);
                dlg->onRunHarmonicAsync();
                waitForBackgroundSolvers();
                dlg->onPlotSelectedHarmonicStep();

                if (dlg->m_tabs) dlg->m_tabs->setCurrentIndex(4);
                dlg->onRunSpectrumAsync();
                waitForBackgroundSolvers();
                dlg->onPlotSpectrumResult();
            }
            break;
        }
        case 27: {
            if (!m_mainWindow->m_analysisSuiteDialog || !m_mainWindow->m_analysisSuiteDialog->isVisible()) {
                m_mainWindow->onOpenAnalysisSuite();
            }
            m_activeDialogStep = 27;
            if (auto* dlg = m_mainWindow->m_analysisSuiteDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                if (dlg->m_tabs) dlg->m_tabs->setCurrentIndex(5);
                if (dlg->m_seisModeCombo) dlg->m_seisModeCombo->setCurrentIndex(0);
                if (dlg->m_seisSchemeCombo) {
                    const int hhtIdx = dlg->m_seisSchemeCombo->findData(static_cast<int>(kernel::TransientScheme::HHTAlpha));
                    if (hhtIdx >= 0) dlg->m_seisSchemeCombo->setCurrentIndex(hhtIdx);
                }
                if (dlg->m_seisStepsSpin) dlg->m_seisStepsSpin->setValue(25);
                dlg->onRunSeismicAsync();
                waitForBackgroundSolvers();

                if (dlg->m_tabs) dlg->m_tabs->setCurrentIndex(6);
                if (dlg->m_nlMethodCombo) {
                    const int nrIdx = dlg->m_nlMethodCombo->findData(static_cast<int>(kernel::NonlinearMethod::NewtonRaphson));
                    if (nrIdx >= 0) dlg->m_nlMethodCombo->setCurrentIndex(nrIdx);
                }
                if (dlg->m_nlKinCombo) {
                    const int coroIdx = dlg->m_nlKinCombo->findData(static_cast<int>(kernel::NonlinearKinematics::Corotational3D));
                    if (coroIdx >= 0) dlg->m_nlKinCombo->setCurrentIndex(coroIdx);
                }
                if (dlg->m_nlFullNewtonCheck) dlg->m_nlFullNewtonCheck->setChecked(true);
                if (dlg->m_nlLineSearchCheck) dlg->m_nlLineSearchCheck->setChecked(true);
                if (dlg->m_nlStepsSpin) dlg->m_nlStepsSpin->setValue(4);

                // O solver não linear corrotacional/J2 opera apenas sobre barras 1D; preserva/restaura
                // temporariamente a malha contínua da Etapa 24 para que ambas as demonstrações E2E concluam.
                const kernel::MeshContainer savedSurfaceMesh = m_mainWindow->m_model.surfaceMesh();
                if (!savedSurfaceMesh.elements.empty()) {
                    m_mainWindow->m_model.surfaceMesh() = kernel::MeshContainer{};
                }
                dlg->onRunNonlinearAsync();
                waitForBackgroundSolvers();
                dlg->onPlayNonlinearHistory();
                if (!savedSurfaceMesh.elements.empty()) {
                    m_mainWindow->m_model.surfaceMesh() = savedSurfaceMesh;
                    if (m_mainWindow->m_lastSolverResult.continuumElementResults.empty()) {
                        m_mainWindow->m_lastSolverResult.success = false;
                    }
                }
            }
            break;
        }
        case 28: {
            m_mainWindow->ensureStaticSolvedSync();
            m_mainWindow->updateResultsTables();
            m_mainWindow->onOpenPostControls();
            m_activeDialogStep = 28;
            if (auto* dlg = m_mainWindow->m_postProcessControlDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                if (dlg->m_contourCombo) {
                    const int dispIdx = dlg->m_contourCombo->findData(static_cast<int>(Viewport3D::PostContourMode::Displacement));
                    if (dispIdx >= 0) dlg->m_contourCombo->setCurrentIndex(dispIdx);
                    if (!m_mainWindow->m_model.surfaceMesh().elements.empty()) {
                        const int vmIdx = dlg->m_contourCombo->findData(static_cast<int>(Viewport3D::PostContourMode::VonMisesStress));
                        if (vmIdx >= 0) dlg->m_contourCombo->setCurrentIndex(vmIdx);
                    }
                }
                if (dlg->m_showMeshCheck) dlg->m_showMeshCheck->setChecked(true);
                if (dlg->m_clipAxisCombo) {
                    const int zIdx = dlg->m_clipAxisCombo->findData(static_cast<int>(Viewport3D::ClipAxis::Z));
                    if (zIdx >= 0) dlg->m_clipAxisCombo->setCurrentIndex(zIdx);
                    if (dlg->m_clipOffsetSpin) dlg->m_clipOffsetSpin->setValue(1.5);
                }
                if (dlg->m_btnPlayPause && dlg->m_btnPlayPause->isEnabled() && !dlg->m_btnPlayPause->isChecked()) {
                    dlg->m_btnPlayPause->click();
                }
            }
            break;
        }
        case 29: {
            m_mainWindow->ensureStaticSolvedSync();
            m_mainWindow->onOpenNbrCompliance();
            m_activeDialogStep = 29;
            if (auto* dlg = m_mainWindow->m_nbrComplianceDialog.data()) {
                positionChildDialogForGuidedSession(dlg);
                dlg->runAudit();
                if (dlg->m_tabs) {
                    for (int t = 0; t < dlg->m_tabs->count(); ++t) {
                        dlg->m_tabs->setCurrentIndex(t);
                        if (QWidget* w = dlg->m_tabs->currentWidget()) {
                            for (auto* tbl : w->findChildren<QTableWidget*>()) {
                                scrollTableToBottomAndTop(tbl);
                            }
                        }
                    }
                    dlg->m_tabs->setCurrentIndex(0);
                }
                if (dlg->m_btnApplyDcrViewport) {
                    dlg->m_btnApplyDcrViewport->click();
                }
            }
            break;
        }
        case 30:
        default: {
            if (m_mainWindow->m_actSelect) m_mainWindow->m_actSelect->setChecked(true);
            viewport->viewIsometric();
            viewport->fitAll();
            break;
        }
    }
}

void GuidedSessionDialog::restoreSelectedExampleToInitialState() {
    if (!m_mainWindow) return;

    waitForBackgroundSolvers();
    closeAuxiliaryInputDialogs();

    if (m_mainWindow->m_actSelect) m_mainWindow->m_actSelect->setChecked(true);
    if (m_mainWindow->m_viewport) {
        m_mainWindow->m_viewport->setInteractionMode(InteractionMode::Select);
        m_mainWindow->m_viewport->clearPostAnimation();
        m_mainWindow->m_viewport->clearElementDcrMap();
        m_mainWindow->m_viewport->setPostContourMode(Viewport3D::PostContourMode::None);
        m_mainWindow->m_viewport->setShowSurfaceMesh(false);
        m_mainWindow->m_viewport->setClipPlane(Viewport3D::ClipAxis::None, 0.0, false);
        m_mainWindow->m_viewport->viewIsometric();
    }

    if (m_mainWindow->m_planeCombo) {
        const int xyIdx = m_mainWindow->m_planeCombo->findData(static_cast<int>(WorkPlane::XY_Top));
        if (xyIdx >= 0) m_mainWindow->m_planeCombo->setCurrentIndex(xyIdx);
    }
    if (m_mainWindow->m_planeOffsetSpin) m_mainWindow->m_planeOffsetSpin->setValue(0.0);
    if (m_mainWindow->m_gridStepSpin) m_mainWindow->m_gridStepSpin->setValue(0.5);
    if (m_mainWindow->m_actSnap) m_mainWindow->m_actSnap->setChecked(true);
    if (m_mainWindow->m_actLocalAxes) m_mainWindow->m_actLocalAxes->setChecked(false);
    if (m_mainWindow->m_quickXSpin) m_mainWindow->m_quickXSpin->setValue(0.0);
    if (m_mainWindow->m_quickYSpin) m_mainWindow->m_quickYSpin->setValue(0.0);
    if (m_mainWindow->m_quickZSpin) m_mainWindow->m_quickZSpin->setValue(0.0);
    if (m_mainWindow->m_actReactions) m_mainWindow->m_actReactions->setChecked(true);
    if (m_mainWindow->m_scaleSlider) m_mainWindow->m_scaleSlider->setValue(30);
    if (m_mainWindow->m_diagramCombo) {
        const int noneIdx = m_mainWindow->m_diagramCombo->findData(static_cast<int>(DiagramType::None));
        if (noneIdx >= 0) m_mainWindow->m_diagramCombo->setCurrentIndex(noneIdx);
    }
    if (m_mainWindow->m_resultsTabs && m_mainWindow->m_resultsTabs->count() > 0) {
        m_mainWindow->m_resultsTabs->setCurrentIndex(0);
    }
    if (m_mainWindow->m_postProcessControlDialog) {
        m_mainWindow->m_postProcessControlDialog->syncFromViewport();
    }

    const int exNum = selectedExampleNumber();
    if (exNum == 1) {
        m_mainWindow->loadExampleBuilding();
    } else if (exNum == 2) {
        m_mainWindow->loadExampleCantilever();
    } else {
        m_mainWindow->loadExampleSpaceTruss();
    }

    waitForBackgroundSolvers();
    if (!m_mainWindow->m_lastSolverResult.success) {
        m_mainWindow->ensureStaticSolvedSync();
    }

    m_mainWindow->m_model.clearSelection();
    m_mainWindow->onSelectionChanged();
    if (m_mainWindow->m_viewport) {
        m_mainWindow->m_viewport->fitAll();
        m_mainWindow->m_viewport->update();
    }
}

void GuidedSessionDialog::finishSession() {
    m_running = false;
    m_paused = false;
    m_completed = true;
    m_currentPhase = 0;
    m_customPhaseIntervalMs = -1;
    if (m_countdownTimer && m_countdownTimer->isActive()) m_countdownTimer->stop();
    clearVisualHighlight();
    m_progressBar->setValue(kTotalSteps);
    QToolTip::hideText();
    restoreSelectedExampleToInitialState();
    refreshCurrentStepTexts();
    updateControlsState();
    if (m_mainWindow) {
        m_mainWindow->showStatusMessage(locale::GerenciadorTraducao::trText(
            "Sessão Guiada concluída com sucesso! Exemplo selecionado restaurado ao estado inicial."));
    }
}

} // namespace omniframe
