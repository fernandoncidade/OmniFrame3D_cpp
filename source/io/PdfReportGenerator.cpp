#include "PdfReportGenerator.hpp"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace omniframe::io {

namespace {

QString toLowerQ(const std::string& s) {
    return QString::fromStdString(s).toLower();
}

bool looksLikeConcrete(const Material& mat) {
    const QString n = toLowerQ(mat.name);
    if (n.contains("concret") || n.startsWith("c2") || n.startsWith("c3") ||
        n.startsWith("c4") || n.startsWith("c5") || n.startsWith("c6") || n.startsWith("c7") || n.startsWith("c8")) {
        return true;
    }
    return (mat.E > 1.5e7 && mat.E < 6.5e7 && mat.gamma >= 20.0 && mat.gamma <= 28.0);
}

bool looksLikeSteel(const Material& mat, const CrossSection& sec) {
    const QString n = toLowerQ(mat.name);
    if (n.contains("aço") || n.contains("aco") || n.contains("steel") ||
        n.contains("astm") || n.contains("a36") || n.contains("a572")) {
        return true;
    }
    if (sec.shape == SectionShape::IShape || sec.shape == SectionShape::WeldedIShape ||
        sec.shape == SectionShape::BoxRHS || sec.shape == SectionShape::ChannelC) {
        return mat.E >= 1.5e8;
    }
    return (mat.E >= 1.7e8 && mat.gamma >= 70.0);
}

double inferConcreteFckMPa(const Material& mat, double overrideMPa) {
    if (overrideMPa > 10.0) return overrideMPa;
    const QString n = QString::fromStdString(mat.name);
    // Procura padrao C20, C25, C30, ..., C90
    for (int f = 90; f >= 15; f -= 5) {
        if (n.contains(QString("C%1").arg(f), Qt::CaseInsensitive)) return static_cast<double>(f);
    }
    // Fallback por modulo tangente aproximado (Eci ~ 5600*sqrt(fck) MPa => E [kPa] = E_MPa*1000)
    const double eMPa = mat.E * 1e-3;
    if (eMPa > 15000.0 && eMPa < 60000.0) {
        const double est = std::pow(eMPa / (5600.0 * 0.85), 2);
        return std::clamp(std::round(est / 5.0) * 5.0, 20.0, 70.0);
    }
    return 30.0;
}

double inferSteelFyMPa(const Material& mat, double overrideMPa) {
    if (overrideMPa > 50.0) return overrideMPa;
    if (mat.isPlastic && mat.sigmaY > 5.0e4) return mat.sigmaY * 1e-3;
    const QString n = toLowerQ(mat.name);
    if (n.contains("572") || n.contains("gr50") || n.contains("345")) return 345.0;
    if (n.contains("a992")) return 345.0;
    if (n.contains("250") || n.contains("a36")) return 250.0;
    return 250.0;
}

const ElementExtremes* findElementExtremes(const SolverResult& res, ElementId eid) {
    for (const auto& ex : res.elementExtremes) {
        if (ex.elementId == eid) return &ex;
    }
    return nullptr;
}

QString fmtNum(double v, int prec = 2) {
    return QString::number(v, 'f', prec);
}

QString fmtSci(double v, int prec = 3) {
    return QString::number(v, 'g', prec);
}

QString sectionShapeLabel(SectionShape sh) {
    switch (sh) {
    case SectionShape::Rectangular: return "Retangular";
    case SectionShape::CircularSolid: return "Circular Solida";
    case SectionShape::CircularTube: return "Tubo Circular";
    case SectionShape::IShape: return "Perfil I/H Laminado";
    case SectionShape::BoxRHS: return "Tubo Retangular RHS";
    case SectionShape::ChannelC: return "Perfil U / Canal C";
    case SectionShape::AngleL: return "Cantoneira L";
    case SectionShape::TShape: return "Perfil T";
    case SectionShape::WeldedIShape: return "Perfil I Soldado";
    case SectionShape::Polygon: return "Poligonal Customizada";
    case SectionShape::Composite: return "Secao Composta";
    case SectionShape::Generic: return "Generica";
    }
    return "Secao";
}

/// Helper stateful para layout de paginas A4 (pontos logicos em 96/300 DPI escalados de forma limpa).
class PdfCanvasLayout {
public:
    PdfCanvasLayout(QPdfWriter& writer, QPainter& painter, QString projectTitle)
        : m_writer(writer), m_p(painter), m_title(std::move(projectTitle))
    {
        const QRectF pageRect = m_writer.pageLayout().paintRectPixels(m_writer.resolution());
        m_pageW = pageRect.width();
        m_pageH = pageRect.height();
        m_marginX = 0.055 * m_pageW;
        m_marginTop = 0.068 * m_pageH;
        m_marginBot = 0.055 * m_pageH;
        m_contentW = m_pageW - 2.0 * m_marginX;
        // Escala de fonte proporcional ao DPI de 300 (1 px = ~0.24 pt; QFont com pointSize ja escala por DPI no QPdfWriter)
        m_scale = static_cast<double>(m_writer.resolution()) / 96.0;
        drawRunningHeaderFooter();
        m_y = m_marginTop;
    }

    double left() const { return m_marginX; }
    double right() const { return m_pageW - m_marginX; }
    double contentW() const { return m_contentW; }
    double curY() const { return m_y; }
    double scale() const { return m_scale; }
    double maxBottom() const { return m_pageH - m_marginBot; }

    void advance(double dyLogical) { m_y += dyLogical * m_scale; }

    void ensureSpace(double heightLogical) {
        if (m_y + heightLogical * m_scale > maxBottom()) {
            newPage();
        }
    }

    void newPage() {
        m_writer.newPage();
        m_pageIndex++;
        drawRunningHeaderFooter();
        m_y = m_marginTop;
    }

    void drawCoverHero(const PdfReportOptions& opts,
                       const StructuralModel& model,
                       const SolverResult& res,
                       const ModelNbrAudit& audit) {
        // Faixa azul naval superior
        const QColor navy(18, 40, 74);
        const QColor accent(29, 114, 184);
        m_p.fillRect(QRectF(m_marginX, m_y, m_contentW, 92.0 * m_scale), navy);
        m_p.fillRect(QRectF(m_marginX, m_y + 88.0 * m_scale, m_contentW, 4.0 * m_scale), accent);

        m_p.setPen(Qt::white);
        QFont fBrand("Segoe UI", 9, QFont::DemiBold);
        m_p.setFont(fBrand);
        m_p.drawText(QRectF(m_marginX + 14.0 * m_scale, m_y + 10.0 * m_scale, m_contentW - 28.0 * m_scale, 20.0 * m_scale),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     "OMNIFRAME 3D  |  KERNEL FEM C++23 + EIGEN 3.4.0  |  PROCESSAMENTO 100% LOCAL");

        QFont fTitle("Segoe UI", 16, QFont::Bold);
        m_p.setFont(fTitle);
        m_p.drawText(QRectF(m_marginX + 14.0 * m_scale, m_y + 30.0 * m_scale, m_contentW - 28.0 * m_scale, 32.0 * m_scale),
                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                     opts.projectTitle);

        QFont fSub("Segoe UI", 9, QFont::Normal);
        m_p.setPen(QColor(210, 224, 240));
        m_p.setFont(fSub);
        const QString sub = QString("Cliente: %1   |   Local: %2   |   Responsavel: %3   |   Data: %4")
            .arg(opts.clientName, opts.siteLocation, opts.engineerName,
                 QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm"));
        m_p.drawText(QRectF(m_marginX + 14.0 * m_scale, m_y + 62.0 * m_scale, m_contentW - 28.0 * m_scale, 22.0 * m_scale),
                     Qt::AlignLeft | Qt::AlignVCenter, sub);

        m_y += 106.0 * m_scale;

        // 4 Cartoes KPI executivos
        struct KpiCard { QString label; QString value; QColor tint; };
        const QColor statusTint = audit.passedAll ? QColor(20, 130, 82) : QColor(185, 38, 52);
        const QString statusTxt = audit.passedAll
            ? QString("APROVADO (DCR %1)").arg(fmtNum(audit.maxDcr, 3))
            : QString("REVISAR (DCR %1)").arg(fmtNum(audit.maxDcr, 3));
        const std::array<KpiCard, 4> cards{{
            {"MALHA / GRAUS DE LIBERDADE",
             QString("%1 Nos / %2 Barras / %3 DOFs").arg(model.getNodes().size()).arg(model.getElements().size()).arg(res.totalDofs),
             QColor(34, 78, 128)},
            {"DESLOCAMENTO MAXIMO",
             QString("%1 mm").arg(fmtNum(res.maxNodalDisplacement * 1000.0, 3)),
             QColor(46, 98, 148)},
            {"EQUILIBRIO GLOBAL",
             res.equilibrium.isBalanced ? QString("OK (Res %1 kN)").arg(fmtSci(res.equilibrium.residualForces.norm(), 2))
                                        : QString("DESBALANCEADO (%1 kN)").arg(fmtSci(res.equilibrium.residualForces.norm(), 2)),
             res.equilibrium.isBalanced ? QColor(22, 125, 86) : QColor(185, 80, 30)},
            {"CONFORMIDADE NBR LOCAL", statusTxt, statusTint}
        }};

        const double gap = 8.0 * m_scale;
        const double cw = (m_contentW - 3.0 * gap) / 4.0;
        const double ch = 52.0 * m_scale;
        for (int i = 0; i < 4; ++i) {
            const double cx = m_marginX + i * (cw + gap);
            QRectF r(cx, m_y, cw, ch);
            m_p.fillRect(r, QColor(246, 248, 252));
            m_p.setPen(QPen(QColor(214, 222, 232), 1.0 * m_scale));
            m_p.drawRect(r);
            m_p.fillRect(QRectF(cx, m_y, cw, 4.0 * m_scale), cards[i].tint);

            m_p.setPen(QColor(88, 98, 112));
            m_p.setFont(QFont("Segoe UI", 7, QFont::DemiBold));
            m_p.drawText(QRectF(cx + 6.0 * m_scale, m_y + 7.0 * m_scale, cw - 12.0 * m_scale, 16.0 * m_scale),
                         Qt::AlignLeft | Qt::AlignVCenter, cards[i].label);

            m_p.setPen(cards[i].tint.darker(115));
            m_p.setFont(QFont("Segoe UI", 8, QFont::Bold));
            m_p.drawText(QRectF(cx + 6.0 * m_scale, m_y + 24.0 * m_scale, cw - 12.0 * m_scale, 24.0 * m_scale),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, cards[i].value);
        }
        m_y += ch + 14.0 * m_scale;
    }

    void drawSectionHeader(int secNum, const QString& title) {
        ensureSpace(28.0);
        const double h = 21.0 * m_scale;
        m_p.fillRect(QRectF(m_marginX, m_y, m_contentW, h), QColor(235, 241, 249));
        m_p.fillRect(QRectF(m_marginX, m_y, 4.0 * m_scale, h), QColor(25, 84, 150));
        m_p.setPen(QColor(18, 42, 74));
        m_p.setFont(QFont("Segoe UI", 10, QFont::Bold));
        m_p.drawText(QRectF(m_marginX + 10.0 * m_scale, m_y, m_contentW - 14.0 * m_scale, h),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QString("%1. %2").arg(secNum).arg(title));
        m_y += h + 6.0 * m_scale;
    }

    void drawParagraph(const QString& text, bool muted = false) {
        QFont f("Segoe UI", 8.5);
        m_p.setFont(f);
        m_p.setPen(muted ? QColor(90, 98, 110) : QColor(32, 38, 46));
        QRectF bound = m_p.boundingRect(QRectF(0, 0, m_contentW, 10000.0 * m_scale),
                                        Qt::AlignLeft | Qt::TextWordWrap, text);
        const double neededLog = (bound.height() / m_scale) + 6.0;
        ensureSpace(neededLog);
        m_p.drawText(QRectF(m_marginX, m_y, m_contentW, bound.height() + 4.0 * m_scale),
                     Qt::AlignLeft | Qt::TextWordWrap, text);
        m_y += bound.height() + 6.0 * m_scale;
    }

    void drawTable(const std::vector<QString>& headers,
                   const std::vector<double>& colFracs,
                   const std::vector<std::vector<QString>>& rows,
                   const QString& emptyMsg = "Nenhum registro aplicavel neste modelo.") {
        if (rows.empty()) {
            drawParagraph(emptyMsg, true);
            return;
        }
        const double rowH = 16.5 * m_scale;
        const double hdrH = 19.0 * m_scale;

        auto drawHdr = [&]() {
            m_p.fillRect(QRectF(m_marginX, m_y, m_contentW, hdrH), QColor(28, 56, 94));
            m_p.setPen(Qt::white);
            m_p.setFont(QFont("Segoe UI", 7.5, QFont::Bold));
            double x = m_marginX;
            for (size_t c = 0; c < headers.size() && c < colFracs.size(); ++c) {
                const double w = colFracs[c] * m_contentW;
                m_p.drawText(QRectF(x + 4.0 * m_scale, m_y, w - 8.0 * m_scale, hdrH),
                             Qt::AlignLeft | Qt::AlignVCenter, headers[c]);
                x += w;
            }
            m_y += hdrH;
        };

        ensureSpace((hdrH + rowH) / m_scale + 4.0);
        drawHdr();

        for (size_t r = 0; r < rows.size(); ++r) {
            if (m_y + rowH > maxBottom()) {
                newPage();
                drawHdr();
            }
            if (r % 2 == 1) {
                m_p.fillRect(QRectF(m_marginX, m_y, m_contentW, rowH), QColor(246, 249, 253));
            }
            m_p.setPen(QPen(QColor(220, 226, 234), 0.6 * m_scale));
            m_p.drawLine(QPointF(m_marginX, m_y + rowH), QPointF(right(), m_y + rowH));

            double x = m_marginX;
            for (size_t c = 0; c < rows[r].size() && c < colFracs.size(); ++c) {
                const double w = colFracs[c] * m_contentW;
                const QString& cell = rows[r][c];
                QColor penCol(34, 40, 48);
                QFont f("Segoe UI", 7.5);
                if (cell == "OK" || cell.startsWith("OK ")) {
                    penCol = QColor(18, 128, 76);
                    f.setBold(true);
                } else if (cell == "FALHA" || cell.startsWith("FALHA") || cell == "FORA DO ESCOPO") {
                    penCol = QColor(178, 36, 48);
                    f.setBold(true);
                } else if (cell == "PARCIAL") {
                    penCol = QColor(175, 110, 12);
                    f.setBold(true);
                }
                m_p.setPen(penCol);
                m_p.setFont(f);
                m_p.drawText(QRectF(x + 4.0 * m_scale, m_y, w - 8.0 * m_scale, rowH),
                             Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, cell);
                x += w;
            }
            m_y += rowH;
        }
        m_y += 8.0 * m_scale;
    }

    void drawElementDiagramPanel(const StructuralModel& model, const Element& el) {
        ensureSpace(125.0);
        const Node* n1 = model.findNode(el.nodeStart);
        const Node* n2 = model.findNode(el.nodeEnd);
        if (!n1 || !n2) return;
        const double L = (n2->position - n1->position).norm();

        constexpr int kSamples = 25;
        std::vector<double> xis(kSamples), Nv(kSamples), Vv(kSamples), Mv(kSamples), Wv(kSamples);
        for (int i = 0; i < kSamples; ++i) {
            const double xi = static_cast<double>(i) / static_cast<double>(kSamples - 1);
            xis[i] = xi;
            const auto f = el.evaluateAt(xi, model);
            Nv[i] = f.N;
            Vv[i] = (std::abs(f.Vz) >= std::abs(f.Vy)) ? f.Vz : f.Vy;
            Mv[i] = (std::abs(f.My) >= std::abs(f.Mz)) ? f.My : f.Mz;
            Wv[i] = std::hypot(f.defLocal.y, f.defLocal.z) * 1000.0; // mm
        }

        const double boxH = 112.0 * m_scale;
        QRectF panel(m_marginX, m_y, m_contentW, boxH);
        m_p.fillRect(panel, QColor(250, 252, 255));
        m_p.setPen(QPen(QColor(200, 212, 226), 0.9 * m_scale));
        m_p.drawRect(panel);

        m_p.setPen(QColor(22, 48, 84));
        m_p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        m_p.drawText(QRectF(m_marginX + 8.0 * m_scale, m_y + 4.0 * m_scale, m_contentW - 16.0 * m_scale, 16.0 * m_scale),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QString("Barra #%1 (Nos %2 -> %3, L = %4 m) - Diagramas Vetoriais N, V, M, Deflexao")
                         .arg(el.id).arg(el.nodeStart).arg(el.nodeEnd).arg(fmtNum(L, 2)));

        struct SubPlot { QString title; const std::vector<double>* vals; QColor col; };
        const std::array<SubPlot, 4> plots{{
            {"N [kN]", &Nv, QColor(24, 108, 178)},
            {"V [kN]", &Vv, QColor(28, 146, 98)},
            {"M [kN.m]", &Mv, QColor(192, 54, 48)},
            {"|w| [mm]", &Wv, QColor(126, 72, 168)}
        }};

        const double gap = 8.0 * m_scale;
        const double pw = (m_contentW - 16.0 * m_scale - 3.0 * gap) / 4.0;
        const double py = m_y + 24.0 * m_scale;
        const double ph = boxH - 32.0 * m_scale;

        for (int p = 0; p < 4; ++p) {
            const double px = m_marginX + 8.0 * m_scale + p * (pw + gap);
            QRectF pr(px, py, pw, ph);
            m_p.fillRect(pr, Qt::white);
            m_p.setPen(QPen(QColor(222, 228, 236), 0.6 * m_scale));
            m_p.drawRect(pr);

            const auto& vals = *plots[p].vals;
            double vAbsMax = 1e-9;
            double vMin = vals.front(), vMax = vals.front();
            for (double v : vals) {
                vAbsMax = std::max(vAbsMax, std::abs(v));
                vMin = std::min(vMin, v);
                vMax = std::max(vMax, v);
            }
            const double zeroY = py + 0.56 * ph;
            m_p.setPen(QPen(QColor(180, 186, 196), 0.7 * m_scale, Qt::DashLine));
            m_p.drawLine(QPointF(px + 4.0 * m_scale, zeroY), QPointF(px + pw - 4.0 * m_scale, zeroY));

            m_p.setPen(QPen(plots[p].col, 1.4 * m_scale));
            const double usableW = pw - 10.0 * m_scale;
            const double ampH = 0.34 * ph;
            for (int i = 0; i + 1 < kSamples; ++i) {
                const double x0 = px + 5.0 * m_scale + xis[i] * usableW;
                const double x1 = px + 5.0 * m_scale + xis[i + 1] * usableW;
                const double y0 = zeroY - (vals[i] / vAbsMax) * ampH;
                const double y1 = zeroY - (vals[i + 1] / vAbsMax) * ampH;
                m_p.drawLine(QPointF(x0, y0), QPointF(x1, y1));
            }
            m_p.setPen(QColor(50, 58, 70));
            m_p.setFont(QFont("Segoe UI", 6.8, QFont::Bold));
            m_p.drawText(QRectF(px + 4.0 * m_scale, py + 2.0 * m_scale, pw - 8.0 * m_scale, 13.0 * m_scale),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString("%1  [%2 .. %3]").arg(plots[p].title, fmtNum(vMin, 2), fmtNum(vMax, 2)));
        }

        m_y += boxH + 8.0 * m_scale;
    }

    void drawImageBlock(const QImage& img, const QString& caption) {
        if (img.isNull()) return;
        const double maxW = m_contentW;
        const double maxH = 220.0 * m_scale;
        const double aspect = static_cast<double>(img.width()) / std::max(1, img.height());
        double drawW = maxW;
        double drawH = drawW / aspect;
        if (drawH > maxH) {
            drawH = maxH;
            drawW = drawH * aspect;
        }
        ensureSpace((drawH / m_scale) + 28.0);
        const double x0 = m_marginX + 0.5 * (m_contentW - drawW);
        QRectF target(x0, m_y, drawW, drawH);
        m_p.drawImage(target, img);
        m_p.setPen(QPen(QColor(180, 192, 210), 1.0 * m_scale));
        m_p.drawRect(target);
        m_y += drawH + 4.0 * m_scale;
        drawParagraph(caption, true);
    }

private:
    void drawRunningHeaderFooter() {
        m_p.setPen(QColor(125, 136, 152));
        m_p.setFont(QFont("Segoe UI", 7.5));
        m_p.drawText(QRectF(m_marginX, 0.02 * m_pageH, m_contentW * 0.7, 0.03 * m_pageH),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     m_title);
        m_p.drawText(QRectF(m_marginX + m_contentW * 0.7, 0.02 * m_pageH, m_contentW * 0.3, 0.03 * m_pageH),
                     Qt::AlignRight | Qt::AlignVCenter,
                     "OmniFrame 3D  |  ABNT NBR Local");
        m_p.setPen(QPen(QColor(216, 224, 234), 0.7 * m_scale));
        m_p.drawLine(QPointF(m_marginX, 0.052 * m_pageH), QPointF(right(), 0.052 * m_pageH));
        m_p.drawLine(QPointF(m_marginX, m_pageH - 0.042 * m_pageH), QPointF(right(), m_pageH - 0.042 * m_pageH));

        m_p.setPen(QColor(125, 136, 152));
        m_p.drawText(QRectF(m_marginX, m_pageH - 0.040 * m_pageH, m_contentW * 0.75, 0.025 * m_pageH),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     "Unidades: [kN, m, s, MPa]  |  Processamento offline sem telemetria.");
        m_p.drawText(QRectF(m_marginX + m_contentW * 0.75, m_pageH - 0.040 * m_pageH, m_contentW * 0.25, 0.025 * m_pageH),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString("Pagina %1").arg(m_pageIndex));
    }

    QPdfWriter& m_writer;
    QPainter& m_p;
    QString m_title;
    double m_pageW = 0.0;
    double m_pageH = 0.0;
    double m_marginX = 0.0;
    double m_marginTop = 0.0;
    double m_marginBot = 0.0;
    double m_contentW = 0.0;
    double m_scale = 1.0;
    double m_y = 0.0;
    int m_pageIndex = 1;
};

} // namespace

ModelNbrAudit PdfReportGenerator::evaluateModelNbrCompliance(const StructuralModel& model,
                                                             const SolverResult& result,
                                                             const PdfReportOptions& opts) {
    ModelNbrAudit audit;
    audit.coverageMatrix = kernel::codes::NbrCodeChecks::getStandardCoverageMatrix();

    kernel::codes::WindInput6123 wIn{opts.windV0_ms, opts.windS1, opts.windS2, opts.windS3};
    audit.sampleWind6123 = kernel::codes::NbrCodeChecks::evaluateWind6123(wIn);

    double maxDisp = result.maxNodalDisplacement;
    double maxZ = 0.0;
    double minZ = 0.0;
    bool firstZ = true;
    for (const auto& nd : model.getNodes()) {
        if (firstZ) { minZ = maxZ = nd.position.z; firstZ = false; }
        else { minZ = std::min(minZ, nd.position.z); maxZ = std::max(maxZ, nd.position.z); }
        maxDisp = std::max(maxDisp, nd.displacementNorm());
    }
    double maxHorizontalDrift = 0.0;
    for (const auto& nd : model.getNodes()) {
        maxHorizontalDrift = std::max(maxHorizontalDrift, std::hypot(nd.displacement[0], nd.displacement[1]));
    }
    double avgSpan = 5.0;
    double sumSpan = 0.0;
    int beamCount = 0;

    auto updateMax = [&](double dcr, const QString& desc) {
        if (dcr > audit.maxDcr) {
            audit.maxDcr = dcr;
            audit.governingSummary = desc;
        }
        if (dcr > 1.0 + 1e-6) audit.passedAll = false;
    };

    for (const auto& el : model.getElements()) {
        const Node* n1 = model.findNode(el.nodeStart);
        const Node* n2 = model.findNode(el.nodeEnd);
        const Material* mat = model.findMaterial(el.materialId);
        const CrossSection* sec = model.findCrossSection(el.sectionId);
        if (!n1 || !n2 || !mat || !sec) continue;

        const Vec3 dp = n2->position - n1->position;
        const double L = std::max(0.05, dp.norm());
        const bool isColumn = (std::abs(dp.z) / L) > 0.707;
        if (!isColumn) { sumSpan += L; ++beamCount; }

        const ElementExtremes* ex = findElementExtremes(result, el.id);
        double N_maxAbs = 0.0, N_signedDom = 0.0;
        double Vy_abs = 0.0, Vz_abs = 0.0, My_abs = 0.0, Mz_abs = 0.0;
        if (ex) {
            N_signedDom = (std::abs(ex->N_min) > std::abs(ex->N_max)) ? ex->N_min : ex->N_max;
            N_maxAbs = std::abs(N_signedDom);
            Vy_abs = std::max(std::abs(ex->Vy_min), std::abs(ex->Vy_max));
            Vz_abs = std::max(std::abs(ex->Vz_min), std::abs(ex->Vz_max));
            My_abs = std::max(std::abs(ex->My_min), std::abs(ex->My_max));
            Mz_abs = std::max(std::abs(ex->Mz_min), std::abs(ex->Mz_max));
        } else if (el.hasResults) {
            for (int k = 0; k <= 10; ++k) {
                const auto f = el.evaluateAt(static_cast<double>(k) / 10.0, model);
                if (std::abs(f.N) > N_maxAbs) {
                    N_maxAbs = std::abs(f.N);
                    N_signedDom = f.N;
                }
                Vy_abs = std::max(Vy_abs, std::abs(f.Vy));
                Vz_abs = std::max(Vz_abs, std::abs(f.Vz));
                My_abs = std::max(My_abs, std::abs(f.My));
                Mz_abs = std::max(Mz_abs, std::abs(f.Mz));
            }
        }

        if (looksLikeSteel(*mat, *sec)) {
            kernel::codes::SteelSectionSpec sp;
            sp.fy_MPa = inferSteelFyMPa(*mat, opts.steelFyOverrideMPa);
            sp.fu_MPa = std::max(sp.fy_MPa * 1.35, 400.0);
            sp.E_MPa = mat->E * 1e-3;
            sp.G_MPa = mat->G() * 1e-3;
            sp.Lx_m = L;
            sp.Ly_m = L;
            sp.Lz_m = L;
            sp.Lb_m = L;

            const auto f0 = el.evaluateAt(0.0, model);
            const auto fA = el.evaluateAt(0.25, model);
            const auto fB = el.evaluateAt(0.50, model);
            const auto fC = el.evaluateAt(0.75, model);
            const auto f1 = el.evaluateAt(1.0, model);
            const double mMaxSampling = std::max({My_abs, std::abs(f0.My), std::abs(fA.My),
                                                  std::abs(fB.My), std::abs(fC.My), std::abs(f1.My)});
            sp.Cb = kernel::codes::NbrCodeChecks::computeCb8800(
                mMaxSampling, std::abs(fA.My), std::abs(fB.My), std::abs(fC.My));

            if (sec->shape == SectionShape::WeldedIShape) {
                sp.kind = kernel::codes::SteelSectionKind::IShapeWelded;
                sp.d_m = sec->dim1;
                sp.bf_m = std::max(sec->dim2, sec->dim5);
                sp.tf_m = std::max(sec->dim3, sec->dim6);
                sp.tw_m = sec->dim4;
            } else if (sec->shape == SectionShape::IShape) {
                sp.kind = kernel::codes::SteelSectionKind::IShapeRolled;
                sp.d_m = sec->dim1;
                sp.bf_m = sec->dim2;
                sp.tf_m = sec->dim3;
                sp.tw_m = sec->dim4;
            } else if (sec->shape == SectionShape::BoxRHS) {
                sp.kind = kernel::codes::SteelSectionKind::RectangularTube;
                sp.d_m = sec->dim1;
                sp.bf_m = sec->dim2;
                sp.tf_m = sec->dim3;
                sp.tw_m = sec->dim4;
            } else if (sec->shape == SectionShape::CircularTube) {
                sp.kind = kernel::codes::SteelSectionKind::CircularTube;
                sp.d_m = sec->dim1;
                sp.bf_m = sec->dim1;
                sp.tf_m = sec->dim2;
                sp.tw_m = sec->dim2;
            } else if (sec->shape == SectionShape::CircularSolid) {
                sp.kind = kernel::codes::SteelSectionKind::CircularSolid;
                sp.d_m = sec->dim1;
                sp.bf_m = sec->dim1;
            } else {
                sp.kind = kernel::codes::SteelSectionKind::RectangularSolid;
                sp.bf_m = sec->dim1;
                sp.d_m = sec->dim2;
            }

            kernel::codes::SteelDesignForces sf;
            sf.N_Sd_kN = N_signedDom;
            sf.Mx_Sd_kNm = My_abs; // Eixo local y = maior inercia (My, Vz)
            sf.My_Sd_kNm = Mz_abs; // Eixo local z = menor inercia (Mz, Vy)
            sf.Vy_Sd_kN = Vz_abs;
            sf.Vx_Sd_kN = Vy_abs;
            (void)N_maxAbs;

            const auto res8800 = kernel::codes::NbrCodeChecks::checkSteelMember8800(sp, sf, opts.situation);
            audit.steelMembers.push_back({
                el.id,
                QString::fromStdString(sec->name),
                QString::fromStdString(mat->name),
                L,
                sf,
                res8800
            });
            updateMax(res8800.dcrMax, QString("Barra Aco #%1 - %2").arg(el.id).arg(QString::fromStdString(res8800.governingLimitState)));
        } else if (looksLikeConcrete(*mat)) {
            const double fck = inferConcreteFckMPa(*mat, opts.concreteFckOverrideMPa);
            const double bSec = (sec->shape == SectionShape::Rectangular) ? sec->dim1 : std::sqrt(std::max(1e-4, sec->A));
            const double hSec = (sec->shape == SectionShape::Rectangular) ? sec->dim2 : std::sqrt(std::max(1e-4, sec->A));
            if (isColumn) {
                kernel::codes::ConcreteColumnSpec cs;
                cs.bx_m = bSec;
                cs.hy_m = hSec;
                cs.fck_MPa = fck;
                cs.rebarGrade = opts.rebarGrade;
                cs.situation = opts.situation;
                // Armadura longitudinal nominal padrao de avaliacao (1.0% Ac) caso modelo nao detalhe barras
                cs.totalAs_cm2 = std::max(0.01 * bSec * hSec * 1e4, 4.0);

                kernel::codes::ConcreteColumnForces cf;
                cf.N_Sd_kN = N_signedDom;
                cf.Mx_Sd_kNm = My_abs;
                cf.My_Sd_kNm = Mz_abs;

                const auto cRes = kernel::codes::NbrCodeChecks::checkConcreteColumnBiaxial6118(cs, cf);
                audit.concreteColumns.push_back({
                    el.id,
                    QString::fromStdString(sec->name),
                    QString::fromStdString(mat->name),
                    L,
                    cf,
                    cRes
                });
                updateMax(cRes.dcrMax, QString("Pilar Concreto #%1 - %2").arg(el.id).arg(QString::fromStdString(cRes.governingLimitState)));
            } else {
                kernel::codes::ConcreteBeamSpec bs;
                bs.bw_m = bSec;
                bs.h_m = hSec;
                bs.d_m = std::max(0.04, hSec - 0.04);
                bs.dp_m = 0.04;
                bs.fck_MPa = fck;
                bs.rebarGrade = opts.rebarGrade;
                bs.situation = opts.situation;
                bs.caa = opts.caa;

                kernel::codes::ConcreteBeamForces bf;
                bf.M_Sd_kNm = (My_abs >= Mz_abs) ? My_abs : Mz_abs;
                bf.V_Sd_kN = (My_abs >= Mz_abs) ? Vz_abs : Vy_abs;
                bf.N_Sd_kN = N_signedDom;
                bf.M_Sd_els_kNm = 0.70 * bf.M_Sd_kNm; // Aproximacao combinacao frequente

                const auto bRes = kernel::codes::NbrCodeChecks::checkConcreteBeam6118(bs, bf);
                audit.concreteBeams.push_back({
                    el.id,
                    QString::fromStdString(sec->name),
                    QString::fromStdString(mat->name),
                    L,
                    bf,
                    bRes
                });
                updateMax(bRes.dcrMax, QString("Viga Concreto #%1 - %2").arg(el.id).arg(QString::fromStdString(bRes.governingLimitState)));
            }
        }
    }

    if (beamCount > 0) avgSpan = sumSpan / static_cast<double>(beamCount);

    // Fundacoes nos apoios verticais
    for (const auto& nd : model.getNodes()) {
        if (!nd.isSupported()) continue;
        if (nd.reaction[2] > 1.0) { // Reacao para cima (+) indica compressao no solo
            kernel::codes::ShallowFoundationInput fin;
            fin.B_m = opts.footingB_m;
            fin.L_m = opts.footingL_m;
            fin.N_k_kN = nd.reaction[2] / 1.4; // Estima valor caracteristico se entrada em ELU
            fin.Mx_k_kNm = nd.reaction[3] / 1.4;
            fin.My_k_kNm = nd.reaction[4] / 1.4;
            fin.sigmaAdm_kPa = opts.soilSigmaAdm_kPa;
            const auto fRes = kernel::codes::NbrCodeChecks::checkShallowFoundation6122(fin);
            audit.foundations.push_back({
                nd.id,
                nd.reaction[2],
                nd.reaction[3],
                nd.reaction[4],
                fin.B_m,
                fin.L_m,
                fRes
            });
            updateMax(fRes.dcrBearing, QString("Sapata Apoio No #%1 (NBR 6122)").arg(nd.id));
        }
    }

    // ELS Global (Tabela 13.3 NBR 6118)
    kernel::codes::ServiceabilityDisplacementInput sinp;
    sinp.span_m = std::max(1.0, avgSpan);
    double maxVertDef = 0.0;
    for (const auto& ex : result.elementExtremes) maxVertDef = std::max(maxVertDef, ex.maxDeflection);
    sinp.midspanDeflectionTotal_m = maxVertDef;
    sinp.midspanDeflectionVibration_m = 0.5 * maxVertDef;
    sinp.buildingHeight_m = std::max(3.0, maxZ - minZ);
    sinp.topHorizontalDriftWind_m = maxHorizontalDrift;
    sinp.storyHeight_m = 3.0;
    sinp.interstoryDriftWind_m = maxHorizontalDrift * std::min(1.0, 3.0 / sinp.buildingHeight_m);
    sinp.partitionSpanDeflection_m = 0.35 * maxVertDef;
    audit.elsDisplacement = kernel::codes::NbrCodeChecks::checkServiceabilityDeflections6118(sinp);
    updateMax(audit.elsDisplacement.dcrMax,
              QString("ELS Deslocamentos (%1)").arg(QString::fromStdString(audit.elsDisplacement.governingCriterion)));

    return audit;
}

bool PdfReportGenerator::generateExecutivePdf(const QString& filePath,
                                              const StructuralModel& model,
                                              const SolverResult& result,
                                              const PdfReportOptions& opts,
                                              QString* errorMsg) {
    if (filePath.trimmed().isEmpty()) {
        if (errorMsg) *errorMsg = "Caminho de saida do PDF vazio.";
        return false;
    }

    const QFileInfo fi(filePath);
    if (!fi.dir().exists() && !QDir().mkpath(fi.dir().absolutePath())) {
        if (errorMsg) *errorMsg = "Nao foi possivel criar diretorio de destino do PDF.";
        return false;
    }

    QPdfWriter writer(filePath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Portrait);
    writer.setResolution(300);
    writer.setTitle(opts.projectTitle);
    writer.setCreator("OmniFrame 3D (C++23 / Eigen 3.4.0)");

    QPainter painter;
    if (!painter.begin(&writer)) {
        if (errorMsg) *errorMsg = "Falha ao inicializar motor grafico QPdfWriter/QPainter.";
        return false;
    }

    const ModelNbrAudit audit = evaluateModelNbrCompliance(model, result, opts);
    PdfCanvasLayout doc(writer, painter, opts.projectTitle);

    // CAPA / SUMARIO EXECUTIVO
    doc.drawCoverHero(opts, model, result, audit);

    // 1. Identificacao e Escopo do Empreendimento
    doc.drawSectionHeader(1, "Identificacao do Projeto e Escopo de Analise");
    doc.drawParagraph(QString(
        "Projeto: %1 | Cliente: %2 | Localizacao: %3 | Engenheiro: %4.\n"
        "Modelo discreto composto por %5 nos, %6 elementos de barra/portico, %7 materiais e %8 secoes transversais, "
        "totalizando %9 graus de liberdade (%10 restringidos). Sistema de unidades adotado: [kN, m, s, MPa].")
        .arg(opts.projectTitle, opts.clientName, opts.siteLocation, opts.engineerName)
        .arg(model.getNodes().size()).arg(model.getElements().size())
        .arg(model.getMaterials().size()).arg(model.getCrossSections().size())
        .arg(result.totalDofs).arg(result.restrainedDofs));

    // 2. Indice Tecnico das 24 Secoes
    doc.drawSectionHeader(2, "Estrutura do Memorial Executivo (24 Secoes)");
    doc.drawParagraph(
        "1. Identificacao | 2. Indice | 3. Metodologia FEM | 4. Matriz Normativa NBR | 5. Vento NBR 6123 | "
        "6. Materiais | 7. Secoes Transversais | 8. Nos e Apoios | 9. Conectividade de Barras | "
        "10. Casos e Combinacoes | 11. Vista 3D do Modelo | 12. Equilibrio Global | "
        "13. Deslocamentos Nodais | 14. Reacoes nos Apoios | 15. Fundacoes NBR 6122 | "
        "16. Esforcos Extremos | 17. Diagramas Vetoriais N/V/M/w | 18. Aco NBR 8800 | "
        "19. Vigas CA NBR 6118 | 20. Pilares CA NBR 6118 | 21. Estado-Limite de Servico ELS | "
        "22. Analise Modal/Flambagem/Nao-Linear | 23. Limites de Escopo Local | 24. Conclusoes e Responsabilidade.", true);

    // 3. Metodologia Numerica FEM
    doc.drawSectionHeader(3, "Metodologia de Analise Estrutural (Kernel C++23 + Eigen 3.4.0)");
    doc.drawParagraph(
        "A rigidez elastica 3D utiliza formulacao exata de Euler-Bernoulli e Timoshenko 6-DOF por no (ou 7-DOF com empenamento de Vlasov), "
        "condensacao estatica exata de rotulas internas, acoplamento por rigid links/diafragmas via multiplicadores ou transformacao mestre-escravo "
        "e integracao de forcas nodais equivalentes em carregamento distribuido, trapezoidal e variacao termica uniforme.");

    // 4. Matriz de Cobertura Normativa Local ABNT NBR
    doc.drawSectionHeader(4, "Rastreabilidade Normativa Frente a Biblioteca Local (Normas_Tecnicas_NBRs)");
    {
        std::vector<std::vector<QString>> rows;
        for (const auto& c : audit.coverageMatrix) {
            QString st = "FORA DO ESCOPO";
            if (c.status == kernel::codes::ComplianceScopeStatus::LocalPdfVerified) st = "OK (PDF Local)";
            else if (c.status == kernel::codes::ComplianceScopeStatus::PartialReferenceOnly) st = "PARCIAL";
            rows.push_back({
                QString::fromStdString(c.codeId),
                st,
                QString::fromStdString(c.scopeNote)
            });
        }
        doc.drawTable({"Norma / Codigo", "Status Local", "Cobertura Verificada e Limitacoes"},
                      {0.22, 0.16, 0.62}, rows);
    }

    // 5. Acoes de Vento (NBR 6123) e Sobrecargas (NBR 6120)
    doc.drawSectionHeader(5, "Acoes Climaticas e Variaveis (ABNT NBR 6123:2023 e ABNT NBR 6120:2019)");
    doc.drawParagraph(QString(
        "Parametros de vento de referencia (Sec. 4.2 ABNT NBR 6123:2023): V0 = %1 m/s, S1 = %2, S2 = %3, S3 = %4 -> "
        "Vk = %5 m/s e pressao dinamica q = %6 N/m2 (%7 kPa). "
        "Divisorias sem posicao definida seguem a Tabela 11 da ABNT NBR 6120:2019 (0.50 / 0.75 / 1.00 kN/m2 ate 3.0 kN/m).")
        .arg(fmtNum(opts.windV0_ms, 1), fmtNum(opts.windS1, 2), fmtNum(opts.windS2, 2), fmtNum(opts.windS3, 2),
             fmtNum(audit.sampleWind6123.vk_ms, 2), fmtNum(audit.sampleWind6123.q_Pa, 1), fmtNum(audit.sampleWind6123.q_kPa, 3)));

    // 6. Materiais
    doc.drawSectionHeader(6, "Propriedades dos Materiais");
    {
        std::vector<std::vector<QString>> rows;
        for (const auto& m : model.getMaterials()) {
            rows.push_back({
                QString::number(m.id),
                QString::fromStdString(m.name),
                fmtNum(m.E * 1e-6, 2),
                fmtNum(m.nu, 2),
                fmtNum(m.rho, 0),
                fmtNum(m.gamma, 2),
                m.isPlastic ? QString("J2 sy=%1 MPa").arg(fmtNum(m.sigmaY * 1e-3, 0)) : "Elastico Linear"
            });
        }
        doc.drawTable({"ID", "Nome", "E [GPa]", "nu", "rho [kg/m3]", "gamma [kN/m3]", "Modelo Constitutivo"},
                      {0.06, 0.28, 0.11, 0.08, 0.13, 0.14, 0.20}, rows);
    }

    // 7. Secoes Transversais
    doc.drawSectionHeader(7, "Propriedades das Secoes Transversais");
    {
        std::vector<std::vector<QString>> rows;
        for (const auto& s : model.getCrossSections()) {
            rows.push_back({
                QString::number(s.id),
                QString::fromStdString(s.name),
                sectionShapeLabel(s.shape),
                fmtNum(s.A * 1e4, 2),
                fmtSci(s.Iy * 1e8, 4),
                fmtSci(s.Iz * 1e8, 4),
                fmtSci(s.J * 1e8, 4)
            });
        }
        doc.drawTable({"ID", "Designacao", "Geometria", "Area [cm2]", "Iy [cm4]", "Iz [cm4]", "J [cm4]"},
                      {0.06, 0.26, 0.20, 0.12, 0.12, 0.12, 0.12}, rows);
    }

    // 8. Nos, Vinculos e Apoios
    doc.drawSectionHeader(8, "Geometria Nodal e Condicoes de Contorno");
    {
        std::vector<std::vector<QString>> rows;
        constexpr size_t kMaxNodesPrint = 40;
        for (size_t i = 0; i < model.getNodes().size() && i < kMaxNodesPrint; ++i) {
            const auto& n = model.getNodes()[i];
            QString bc = "Livre";
            if (n.support.hasFixedDofs()) {
                QString fix;
                static constexpr const char* dofLbl[6] = {"Ux", "Uy", "Uz", "Rx", "Ry", "Rz"};
                for (int d = 0; d < 6; ++d) {
                    if (n.support.fixed[d]) {
                        if (!fix.isEmpty()) fix += ",";
                        fix += dofLbl[d];
                    }
                }
                bc = "Fixo (" + fix + ")";
            } else if (n.support.hasSprings()) {
                bc = "Mola Elastica";
            }
            rows.push_back({
                QString::number(n.id),
                fmtNum(n.position.x, 3),
                fmtNum(n.position.y, 3),
                fmtNum(n.position.z, 3),
                bc
            });
        }
        doc.drawTable({"No", "X [m]", "Y [m]", "Z [m]", "Vinculacao / Restricao"},
                      {0.08, 0.15, 0.15, 0.15, 0.47}, rows);
        if (model.getNodes().size() > kMaxNodesPrint) {
            doc.drawParagraph(QString("Nota: exibidos os primeiros %1 nos de %2 no total.").arg(kMaxNodesPrint).arg(model.getNodes().size()), true);
        }
    }

    // 9. Conectividade de Elementos 1D
    doc.drawSectionHeader(9, "Conectividade, Secao e Formulacao das Barras");
    {
        std::vector<std::vector<QString>> rows;
        constexpr size_t kMaxElPrint = 45;
        for (size_t i = 0; i < model.getElements().size() && i < kMaxElPrint; ++i) {
            const auto& el = model.getElements()[i];
            const Node* n1 = model.findNode(el.nodeStart);
            const Node* n2 = model.findNode(el.nodeEnd);
            const double L = (n1 && n2) ? (n2->position - n1->position).norm() : 0.0;
            QString form = "Euler-Bernoulli 3D";
            if (el.formulation == ElementFormulation::Truss3D) form = "Trelica 3D";
            else if (el.formulation == ElementFormulation::Timoshenko3D) form = "Timoshenko 3D";
            rows.push_back({
                QString::number(el.id),
                QString("%1 -> %2").arg(el.nodeStart).arg(el.nodeEnd),
                fmtNum(L, 3),
                QString::number(el.materialId),
                QString::number(el.sectionId),
                form
            });
        }
        doc.drawTable({"Barra", "Nos (i -> j)", "L [m]", "Mat.", "Sec.", "Formulacao"},
                      {0.10, 0.18, 0.12, 0.10, 0.10, 0.40}, rows);
    }

    // 10. Casos e Combinacoes de Carregamento
    doc.drawSectionHeader(10, "Casos de Carregamento e Combinacoes Normativas");
    {
        std::vector<std::vector<QString>> rows;
        for (const auto& lc : model.getLoadCases()) {
            rows.push_back({
                QString("Caso #%1").arg(lc.id),
                QString::fromStdString(lc.name),
                lc.selfWeight.enabled ? "Sim" : "Nao",
                QString("%1 nodais / %2 barras").arg(lc.nodalLoads.size()).arg(lc.elementLoads.size())
            });
        }
        for (const auto& comb : model.getLoadCombinations()) {
            QString desc;
            for (const auto& f : comb.factors) {
                if (!desc.isEmpty()) desc += " + ";
                desc += QString("%1*C%2").arg(fmtNum(f.gamma, 2)).arg(f.loadCaseId);
            }
            rows.push_back({
                QString("Comb #%1").arg(comb.id),
                QString::fromStdString(comb.name),
                "-",
                desc
            });
        }
        doc.drawTable({"ID", "Nome", "Peso Prop.", "Definicao / Cargas"},
                      {0.14, 0.28, 0.13, 0.45}, rows,
                      "Cargas aplicadas diretamente nos nos e barras (caso ativo padrao).");
    }

    // 11. Vista 3D
    doc.drawSectionHeader(11, "Registro Visual do Modelo 3D");
    if (!opts.viewportSnapshot.isNull()) {
        doc.drawImageBlock(opts.viewportSnapshot, "Figura 1 - Captura ortogonal/perspectiva da viewport OpenGL 3.3 Core do OmniFrame 3D.");
    } else {
        doc.drawParagraph("Snapshot 3D nao anexado (geracao via CLI headless ou modo sem captura de tela).", true);
    }

    // 12. Diagnostico e Equilibrio Global
    doc.drawSectionHeader(12, "Diagnostico do Solver e Equilibrio Estatico Global");
    {
        const auto& eq = result.equilibrium;
        std::vector<std::vector<QString>> rows = {
            {"Tempo de Execucao / DOFs", QString("%1 ms / %2 DOFs (%3 fixos)").arg(fmtNum(result.executionTimeMs, 2)).arg(result.totalDofs).arg(result.restrainedDofs), "OK"},
            {"Forcas Aplicadas (Fx, Fy, Fz)", QString("(%1, %2, %3) kN").arg(fmtNum(eq.sumAppliedForces.x, 3), fmtNum(eq.sumAppliedForces.y, 3), fmtNum(eq.sumAppliedForces.z, 3)), "-"},
            {"Reacoes Totais (Rx, Ry, Rz)", QString("(%1, %2, %3) kN").arg(fmtNum(eq.sumReactions.x, 3), fmtNum(eq.sumReactions.y, 3), fmtNum(eq.sumReactions.z, 3)), "-"},
            {"Residuo de Forcas |SF + SR|", QString("%1 kN").arg(fmtSci(eq.residualForces.norm(), 3)), eq.isBalanced ? "OK" : "FALHA"},
            {"Residuo de Momentos |SM + SMR|", QString("%1 kN.m").arg(fmtSci(eq.residualMoments.norm(), 3)), eq.isBalanced ? "OK" : "FALHA"},
            {"Energia de Deformacao Total", QString("%1 kN.m").arg(fmtNum(result.totalStrainEnergy, 4)), "-"}
        };
        doc.drawTable({"Grandeza / Auditoria", "Valor Calculado", "Status"},
                      {0.35, 0.48, 0.17}, rows);
    }

    // 13. Deslocamentos Nodais
    doc.drawSectionHeader(13, "Deslocamentos e Rotacoes Nodais");
    {
        std::vector<std::vector<QString>> rows;
        constexpr size_t kMaxDispPrint = 35;
        for (size_t i = 0; i < model.getNodes().size() && i < kMaxDispPrint; ++i) {
            const auto& n = model.getNodes()[i];
            rows.push_back({
                QString::number(n.id),
                fmtNum(n.displacement[0] * 1000.0, 3),
                fmtNum(n.displacement[1] * 1000.0, 3),
                fmtNum(n.displacement[2] * 1000.0, 3),
                fmtNum(n.displacement[3] * 1000.0, 3),
                fmtNum(n.displacement[4] * 1000.0, 3),
                fmtNum(n.displacement[5] * 1000.0, 3)
            });
        }
        doc.drawTable({"No", "ux [mm]", "uy [mm]", "uz [mm]", "rx [mrad]", "ry [mrad]", "rz [mrad]"},
                      {0.10, 0.15, 0.15, 0.15, 0.15, 0.15, 0.15}, rows);
    }

    // 14. Reacoes nos Apoios
    doc.drawSectionHeader(14, "Reacoes de Apoio (Forcas e Momentos)");
    {
        std::vector<std::vector<QString>> rows;
        for (const auto& n : model.getNodes()) {
            if (!n.isSupported()) continue;
            rows.push_back({
                QString::number(n.id),
                fmtNum(n.reaction[0], 2),
                fmtNum(n.reaction[1], 2),
                fmtNum(n.reaction[2], 2),
                fmtNum(n.reaction[3], 2),
                fmtNum(n.reaction[4], 2),
                fmtNum(n.reaction[5], 2)
            });
        }
        doc.drawTable({"No", "Rx [kN]", "Ry [kN]", "Rz [kN]", "Mx [kNm]", "My [kNm]", "Mz [kNm]"},
                      {0.10, 0.15, 0.15, 0.15, 0.15, 0.15, 0.15}, rows);
    }

    // 15. Fundacoes Rasas (NBR 6122:2022)
    doc.drawSectionHeader(15, "Verificacao Preliminar de Fundacao Rasa (ABNT NBR 6122:2022 Tab. 1)");
    {
        std::vector<std::vector<QString>> rows;
        for (const auto& fn : audit.foundations) {
            rows.push_back({
                QString::number(fn.nodeId),
                QString("%1 x %2 m").arg(fmtNum(fn.B_m, 2), fmtNum(fn.L_m, 2)),
                fmtNum(fn.reactionVert_kN, 1),
                fmtNum(fn.result.sigmaMax_kPa, 1),
                fmtNum(fn.result.sigmaLimit_kPa, 1),
                fmtNum(fn.result.dcrBearing, 3),
                fn.result.passed ? "OK" : (fn.result.hasUplift ? "FALHA (Levant.)" : "FALHA")
            });
        }
        doc.drawTable({"Apoio", "Sapata Ref.", "Rz [kN]", "sMax [kPa]", "sAdm [kPa]", "DCR", "Status"},
                      {0.09, 0.17, 0.14, 0.15, 0.15, 0.12, 0.18}, rows,
                      "Nenhum apoio vertical comprimido identificado para verificacao de sapata.");
    }

    // 16. Esforcos Internos Extremos nas Barras
    doc.drawSectionHeader(16, "Envoltoria de Esforcos Solicitantes por Barra");
    {
        std::vector<std::vector<QString>> rows;
        constexpr size_t kMaxEx = 35;
        for (size_t i = 0; i < result.elementExtremes.size() && i < kMaxEx; ++i) {
            const auto& ex = result.elementExtremes[i];
            const double Ndom = (std::abs(ex.N_min) > std::abs(ex.N_max)) ? ex.N_min : ex.N_max;
            const double Vmax = std::max({std::abs(ex.Vy_min), std::abs(ex.Vy_max), std::abs(ex.Vz_min), std::abs(ex.Vz_max)});
            const double MyAbs = std::max(std::abs(ex.My_min), std::abs(ex.My_max));
            const double MzAbs = std::max(std::abs(ex.Mz_min), std::abs(ex.Mz_max));
            const double TxAbs = std::max(std::abs(ex.Tx_min), std::abs(ex.Tx_max));
            rows.push_back({
                QString::number(ex.elementId),
                fmtNum(Ndom, 2),
                fmtNum(Vmax, 2),
                fmtNum(TxAbs, 2),
                fmtNum(MyAbs, 2),
                fmtNum(MzAbs, 2),
                fmtNum(ex.maxDeflection * 1000.0, 3)
            });
        }
        doc.drawTable({"Barra", "N [kN]", "Vmax [kN]", "Tx [kNm]", "|My| [kNm]", "|Mz| [kNm]", "wMax [mm]"},
                      {0.10, 0.15, 0.15, 0.14, 0.15, 0.15, 0.16}, rows);
    }

    // 17. Diagramas Vetoriais N / V / M / Deflexao das Barras Mais Solicitadas
    doc.drawSectionHeader(17, "Diagramas Vetoriais N(x), V(x), M(x) e Deflexao das Barras Criticas");
    if (opts.includeDiagrams && !model.getElements().empty()) {
        std::vector<std::pair<double, ElementId>> ranked;
        for (const auto& ex : result.elementExtremes) {
            const double score = std::max(std::abs(ex.My_min), std::abs(ex.My_max)) +
                                 std::max(std::abs(ex.Mz_min), std::abs(ex.Mz_max)) +
                                 0.1 * std::max(std::abs(ex.N_min), std::abs(ex.N_max));
            ranked.emplace_back(score, ex.elementId);
        }
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        const size_t nDiag = std::min<size_t>(3, ranked.size());
        for (size_t i = 0; i < nDiag; ++i) {
            const Element* el = model.findElement(ranked[i].second);
            if (el) doc.drawElementDiagramPanel(model, *el);
        }
    } else {
        doc.drawParagraph("Diagramas vetoriais desabilitados ou modelo sem barras.", true);
    }

    // 18. Aco ABNT NBR 8800:2024
    doc.drawSectionHeader(18, "Verificacao de Barras de Aco (ABNT NBR 8800:2024)");
    {
        std::vector<std::vector<QString>> rows;
        for (const auto& st : audit.steelMembers) {
            rows.push_back({
                QString::number(st.elementId),
                st.sectionName,
                fmtNum(st.result.Nc_Rd_kN, 1),
                fmtNum(st.result.Mx_Rd_kNm, 2),
                fmtNum(st.result.Vy_Rd_kN, 1),
                fmtNum(st.result.dcrInteraction, 3),
                fmtNum(st.result.dcrMax, 3),
                st.result.passed ? "OK" : "FALHA"
            });
        }
        doc.drawTable({"Barra", "Perfil", "Nc,Rd [kN]", "Mx,Rd [kNm]", "Vy,Rd [kN]", "PMM", "DCR Max", "Status"},
                      {0.08, 0.22, 0.13, 0.14, 0.13, 0.10, 0.10, 0.10}, rows,
                      "Nenhuma barra com material metalico / perfil de aco identificada no modelo.");
    }

    // 19. Vigas de Concreto Armado ABNT NBR 6118:2023
    doc.drawSectionHeader(19, "Dimensionamento/Verificacao de Vigas em Concreto Armado (ABNT NBR 6118:2023)");
    {
        std::vector<std::vector<QString>> rows;
        for (const auto& cb : audit.concreteBeams) {
            rows.push_back({
                QString::number(cb.elementId),
                cb.sectionName,
                fmtNum(cb.forces.M_Sd_kNm, 2),
                fmtNum(cb.result.requiredAs_cm2, 2),
                fmtNum(cb.result.requiredAswPerS_cm2PerM, 2),
                fmtNum(cb.result.wk_mm, 3),
                fmtNum(cb.result.dcrMax, 3),
                cb.result.passed ? "OK" : "FALHA"
            });
        }
        doc.drawTable({"Viga", "Secao", "MSd [kNm]", "As,req [cm2]", "Asw/s [cm2/m]", "wk [mm]", "DCR", "Status"},
                      {0.08, 0.19, 0.13, 0.14, 0.15, 0.11, 0.10, 0.10}, rows,
                      "Nenhuma viga horizontal em concreto armado identificada no modelo.");
    }

    // 20. Pilares de Concreto Armado ABNT NBR 6118:2023 (Biaxial P-Mx-My)
    doc.drawSectionHeader(20, "Verificacao Biaxial P-Mx-My de Pilares em Concreto (ABNT NBR 6118:2023)");
    {
        std::vector<std::vector<QString>> rows;
        for (const auto& cc : audit.concreteColumns) {
            rows.push_back({
                QString::number(cc.elementId),
                cc.sectionName,
                fmtNum(cc.forces.N_Sd_kN, 1),
                fmtNum(cc.result.N_Rd_pureComp_kN, 1),
                fmtNum(cc.result.Mx_Rd_atNSd_kNm, 2),
                fmtNum(cc.result.rhoPercent, 2) + "%",
                fmtNum(cc.result.dcrPMM, 3),
                cc.result.passed ? "OK" : "FALHA"
            });
        }
        doc.drawTable({"Pilar", "Secao", "NSd [kN]", "NRd,c [kN]", "Mx,Rd [kNm]", "Taxa", "DCR PMM", "Status"},
                      {0.08, 0.19, 0.13, 0.14, 0.14, 0.10, 0.12, 0.10}, rows,
                      "Nenhum pilar vertical em concreto armado identificado no modelo.");
    }

    // 21. Estado-Limite de Servico (Tabela 13.3 NBR 6118)
    doc.drawSectionHeader(21, "Verificacao do Estado-Limite de Servico (ABNT NBR 6118:2023 Tabela 13.3)");
    {
        const auto& e = audit.elsDisplacement;
        std::vector<std::vector<QString>> rows = {
            {"Aceitabilidade Sensorial/Visual (L/250)", fmtNum(e.dcrTotalDeflection, 3), e.dcrTotalDeflection <= 1.0 ? "OK" : "FALHA"},
            {"Vibracoes em Pisos (L/350)", fmtNum(e.dcrVibrationDeflection, 3), e.dcrVibrationDeflection <= 1.0 ? "OK" : "FALHA"},
            {"Deslocamento Lateral Global (H/1700)", fmtNum(e.dcrGlobalDrift, 3), e.dcrGlobalDrift <= 1.0 ? "OK" : "FALHA"},
            {"Deslocamento Interpavimento (Hi/850)", fmtNum(e.dcrInterstoryDrift, 3), e.dcrInterstoryDrift <= 1.0 ? "OK" : "FALHA"},
            {"Alvenarias e Divisorias min(L/500, 10mm)", fmtNum(e.dcrPartitionDeflection, 3), e.dcrPartitionDeflection <= 1.0 ? "OK" : "FALHA"}
        };
        doc.drawTable({"Criterio ELS (NBR 6118 Tab. 13.3)", "Razao Solicit./Limite", "Status"},
                      {0.58, 0.24, 0.18}, rows);
    }

    // 22. Analises Avancadas (Modal / Flambagem / Nao-Linear)
    doc.drawSectionHeader(22, "Analises Avancadas: Dinamica Modal, Flambagem Elasto-Geometrica e Nao-Linearidade");
    if (opts.modalResult && opts.modalResult->success && !opts.modalResult->modes.empty()) {
        std::vector<std::vector<QString>> mRows;
        for (const auto& m : opts.modalResult->modes) {
            mRows.push_back({
                QString("Modo %1").arg(m.modeIndex),
                fmtNum(m.frequencyHz, 3),
                fmtNum(m.periodSec, 4),
                QString("%1% / %2% / %3%")
                    .arg(fmtNum(m.effectiveMassRatio[0] * 100.0, 1),
                         fmtNum(m.effectiveMassRatio[1] * 100.0, 1),
                         fmtNum(m.effectiveMassRatio[2] * 100.0, 1))
            });
        }
        doc.drawTable({"Modo", "Frequencia [Hz]", "Periodo [s]", "Part. Massa Efetiva (Ux/Uy/Uz)"},
                      {0.18, 0.22, 0.22, 0.38}, mRows);
    } else {
        doc.drawParagraph("Analise modal dinamica nao anexada a este relatorio.", true);
    }

    if (opts.bucklingResult && opts.bucklingResult->success && !opts.bucklingResult->loadFactors.empty()) {
        std::vector<std::vector<QString>> bRows;
        for (size_t i = 0; i < opts.bucklingResult->loadFactors.size(); ++i) {
            bRows.push_back({
                QString("Modo Flambagem %1").arg(i + 1),
                fmtNum(opts.bucklingResult->loadFactors[i], 4),
                opts.bucklingResult->loadFactors[i] >= 1.0 ? "OK (lambda_cr >= 1)" : "CRITICO (< 1)"
            });
        }
        doc.drawTable({"Modo", "Fator Critico lambda_cr", "Avaliacao Estabilidade"},
                      {0.34, 0.33, 0.33}, bRows);
    }

    if (opts.nonlinearResult && !opts.nonlinearResult->history.empty()) {
        doc.drawParagraph(QString(
            "Trajetoria nao-linear executada em %1 passos incrementais, atingindo fator de carga lambda = %2 (sucesso = %3).")
            .arg(opts.nonlinearResult->history.size())
            .arg(fmtNum(opts.nonlinearResult->achievedLoadFactor, 4))
            .arg(opts.nonlinearResult->success ? "Sim" : "Nao"));
    }

    // 23. Limites Explicitos de Escopo Tecnico
    doc.drawSectionHeader(23, "Declaracao Transparente de Escopo e Normas Nao Aplicadas");
    doc.drawParagraph(
        "Os PDFs normativos ausentes na biblioteca local (ABNT NBR 8681, NBR 14762 perfis formados a frio e NBR 15421 sismo), "
        "bem como normas internacionais (AISC 360, Eurocode), estao explicitamente fora de escopo neste memorial. "
        "Para concreto armado, os resultados refletem verificacoes de secao transversal basica (armadura de pilar adotada em 1% Ac quando nao detalhada no modelo). "
        "Detalhamento executivo, ancoragens e emendas devem ser conferidos pelo Engenheiro Responsavel.");

    // 24. Conclusoes e Responsabilidade Tecnica
    doc.drawSectionHeader(24, "Conclusoes Executivas e Termo de Responsabilidade");
    const QString verdict = audit.passedAll
        ? QString("ESTRUTURA ATENDE AOS CRITERIOS VERIFICADOS (DCR maximo = %1 <= 1.000).")
              .arg(fmtNum(audit.maxDcr, 3))
        : QString("NECESSITA REVISAO ESTRUTURAL: DCR maximo = %1 > 1.000 em [%2].")
              .arg(fmtNum(audit.maxDcr, 3), audit.governingSummary);
    doc.drawParagraph(verdict + "\n\n____________________________________________________\n" +
                      opts.engineerName + " - Responsavel Tecnico\n" +
                      "Documento gerado 100% offline pelo OmniFrame 3D.");

    painter.end();
    return true;
}

} // namespace omniframe::io
