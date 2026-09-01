#include "render/TextBrowserBackend.h"

#include "core/MermaidCache.h"

#include <QFileInfo>
#include <QAbstractTextDocumentLayout>
#include <QPaintEvent>
#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <QScrollBar>
#include <QSvgRenderer>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QFont>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTextTable>
#include <QTextTableCell>

namespace {

/// SVG 以 2 倍解析度光柵化，顯示尺寸再由 image format 明確指定，
/// 這樣在 HiDPI 下不會糊，且尺寸完全可控（不依賴 Qt 對 SVG 固有尺寸的推斷）。
constexpr qreal kRasterScale = 2.0;

QImage placeholderImage(int width, const QString &text, const Theme::Colors &c)
{
    const int h = 88;
    QImage img(int(width * kRasterScale), int(h * kRasterScale), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(kRasterScale, kRasterScale);

    QPainterPath path;
    path.addRoundedRect(QRectF(1, 1, width - 2, h - 2), 6, 6);
    p.setPen(QPen(QColor(c.border), 1, Qt::DashLine));
    p.setBrush(QColor(c.codeBackground));
    p.drawPath(path);

    p.setPen(QColor(c.muted));
    p.drawText(QRectF(0, 0, width, h), Qt::AlignCenter, text);
    return img;
}

/// 透明背景的圖片若內容顏色與頁面底色太接近，在該主題下等於看不見。
/// 這裡量可見像素的平均亮度，對比不足就墊一層中性底色（含 padding）。
/// mermaid 的圖不走這裡 —— 它的主題由我們指定，本來就與頁面相符。
QImage backdropIfLowContrast(const QImage &img, const QColor &pageBg)
{
    if (img.isNull() || !img.hasAlphaChannel())
        return img;

    double sum = 0.0;
    int n = 0;
    const int step = qMax(1, qMin(img.width(), img.height()) / 64);
    for (int y = 0; y < img.height(); y += step) {
        for (int x = 0; x < img.width(); x += step) {
            const QColor c = img.pixelColor(x, y);
            if (c.alpha() < 32)
                continue;
            sum += Theme::relativeLuminance(c);
            ++n;
        }
    }
    if (n == 0)
        return img;   // 全透明，沒得救也沒得壞

    const double meanLum = sum / n;
    // 用平均亮度組一個代表灰階來算對比（sRGB 反伽瑪）
    const int v = qBound(0, int(qRound(std::pow(meanLum, 1.0 / 2.2) * 255.0)), 255);
    const QColor representative(v, v, v);

    if (Theme::contrastRatio(representative, pageBg) >= Theme::MinNonTextContrast)
        return img;

    const QColor card = meanLum < 0.5 ? QColor(0xf2, 0xf2, 0xf2) : QColor(0x13, 0x13, 0x13);
    const int pad = 8;
    QImage out(img.width() + pad * 2, img.height() + pad * 2,
               QImage::Format_ARGB32_Premultiplied);
    out.fill(card);
    QPainter p(&out);
    p.drawImage(pad, pad, img);
    return out;
}

QImage rasterizeSvg(const QString &path, int maxWidth, QSize *logicalOut)
{
    QSvgRenderer r(path);
    if (!r.isValid())
        return {};

    QSize sz = r.defaultSize();
    if (!sz.isValid() || sz.isEmpty())
        sz = QSize(600, 320);
    if (sz.width() > maxWidth)
        sz.scale(maxWidth, INT_MAX, Qt::KeepAspectRatio);

    if (logicalOut)
        *logicalOut = sz;

    QImage img(int(sz.width() * kRasterScale), int(sz.height() * kRasterScale),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    r.render(&p);
    return img;
}

} // namespace

// ---------------------------------------------------------------------------
// MdTextBrowser：覆寫資源載入與欄寬置中。
// 刻意不加 Q_OBJECT —— 只覆寫虛擬函式，不需要自己的 signal/slot。
// ---------------------------------------------------------------------------
class MdTextBrowser : public QTextBrowser
{
public:
    explicit MdTextBrowser(MermaidCache *cache, QWidget *parent = nullptr)
        : QTextBrowser(parent)
        , m_cache(cache)
    {
        setOpenLinks(false);           // 連結交給 MainWindow 決定怎麼處理
        setOpenExternalLinks(false);
        setFrameShape(QFrame::NoFrame);

        // 檢視器不需要復原，而下面那幾個 tree walk 會做上百次格式修改，
        // 每一次都會被記進 undo stack。關掉省下大量記憶體與時間。
        document()->setUndoRedoEnabled(false);
    }

    void setMermaidSources(const QVector<MermaidBlock> &blocks)
    {
        m_mermaidSources.clear();
        for (const MermaidBlock &b : blocks)
            m_mermaidSources.insert(b.key, b.source);
    }

    void setDark(bool dark) { m_dark = dark; }

    /// 縮放倍率。標題字級是明確設定的（見 applyHeadingScale），
    /// 不會自動跟著 widget 字型走，所以必須把倍率一起帶進去。
    void setZoomFactor(qreal factor) { m_zoom = factor; }
    qreal zoomFactor() const { return m_zoom; }
    Theme::Mode mode() const { return m_dark ? Theme::Dark : Theme::Light; }

    /// 保證沒有「看不見的文字」。
    ///
    /// markdown 可以內嵌原始 HTML，裡面可能寫死了顏色（`<span style="color:#000">`），
    /// 那在黑色主題下就是隱形的。這裡對每個文字片段算它與「實際背景」的 WCAG
    /// 對比，不足 4.5:1 就換成該背景上讀得到的顏色。
    /// 實際背景的判定順序：片段自己的背景 → 所屬 block 的背景 → 頁面底色。
    void applyContrastFixups()
    {
        const Theme::Mode m = mode();
        const QColor pageBg(Theme::colors(m).background);
        const QColor themeText(Theme::colors(m).text);

        struct Fix {
            int start;
            int end;
            QColor fg;
        };
        QVector<Fix> fixes;

        QTextDocument *doc = document();
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            QColor blockBg = pageBg;
            if (b.blockFormat().background().style() != Qt::NoBrush) {
                const QColor c = b.blockFormat().background().color();
                if (c.isValid() && c.alpha() > 0)
                    blockBg = c;
            }

            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment f = it.fragment();
                if (!f.isValid() || f.charFormat().isImageFormat())
                    continue;
                if (f.text().trimmed().isEmpty())
                    continue;

                const QTextCharFormat cf = f.charFormat();

                QColor bg = blockBg;
                if (cf.background().style() != Qt::NoBrush) {
                    const QColor c = cf.background().color();
                    if (c.isValid() && c.alpha() > 0)
                        bg = c;
                }
                const QColor fg = cf.foreground().style() != Qt::NoBrush
                                      ? cf.foreground().color()
                                      : themeText;

                if (Theme::contrastRatio(fg, bg) < Theme::MinTextContrast)
                    fixes.append({ f.position(), f.position() + f.length(),
                                   Theme::readableOn(bg, m) });
            }
        }

        if (fixes.isEmpty())
            return;
        QTextCursor batch(doc);
        batch.beginEditBlock();
        for (int i = fixes.size() - 1; i >= 0; --i) {
            const Fix &fx = fixes.at(i);
            QTextCursor cur(doc);
            cur.setPosition(fx.start);
            cur.setPosition(fx.end, QTextCursor::KeepAnchor);
            // merge 而非 set：只換前景色，其餘格式（字型、粗體…）保留
            QTextCharFormat merge;
            merge.setForeground(fx.fg);
            cur.mergeCharFormat(merge);
        }
        batch.endEditBlock();
    }

    /// 圖片的邏輯（顯示）尺寸，由 loadResource 記錄、供 applyImageSizing 使用。
    QSize logicalSize(const QString &url) const { return m_logicalSize.value(url); }

    int contentWidth() const
    {
        return qMin(Theme::ContentWidth, qMax(240, viewport()->width() - 32));
    }

    /// Okular 的 converter.cpp 同樣在文件建好後走一遍 tree 修正圖片尺寸。
    /// 這裡除了套用邏輯尺寸，也處理「圖檔不存在」的退化顯示。
    void applyImageSizing()
    {
        struct Fix {
            int start;
            int end;
            QTextImageFormat fmt;
            QString missingLabel;
        };
        QVector<Fix> fixes;

        QTextDocument *doc = document();
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment f = it.fragment();
                if (!f.isValid())
                    continue;
                if (!f.charFormat().isImageFormat())
                    continue;

                QTextImageFormat fmt = f.charFormat().toImageFormat();
                const QString url = fmt.name();

                // 若排版還沒跑過（例如 widget 當時是隱藏的），loadResource 就
                // 沒被呼叫過，這裡主動要一次把尺寸補上。
                if (!m_logicalSize.contains(url) && !m_known.contains(url))
                    doc->resource(QTextDocument::ImageResource, QUrl(url));

                const QSize logical = m_logicalSize.value(url);

                if (logical.isValid() && !logical.isEmpty()) {
                    fmt.setWidth(logical.width());
                    fmt.setHeight(logical.height());
                    fixes.append({ f.position(), f.position() + f.length(), fmt, QString() });
                } else if (m_known.contains(url) && !m_known.value(url)) {
                    // 資源載入失敗：用檔名做退化標示（QTextImageFormat 不保留 alt）
                    const QUrl u(url);
                    const QString name = u.isLocalFile()
                                             ? QFileInfo(u.toLocalFile()).fileName()
                                             : url;
                    fixes.append({ f.position(), f.position() + f.length(), fmt,
                                   QStringLiteral("[缺少圖片: %1]").arg(name) });
                }
            }
        }

        // 收集完再改，避免邊走邊改文件。
        // 整批包進一個 edit block：否則每一次 setCharFormat 都會觸發一次
        // 重新排版，在有大量片段的文件上是二次方級的成本。
        if (fixes.isEmpty())
            return;
        QTextCursor batch(doc);
        batch.beginEditBlock();
        for (int i = fixes.size() - 1; i >= 0; --i) {
            const Fix &fx = fixes.at(i);
            QTextCursor cur(doc);
            cur.setPosition(fx.start);
            cur.setPosition(fx.end, QTextCursor::KeepAnchor);
            if (fx.missingLabel.isEmpty()) {
                cur.setCharFormat(fx.fmt);
            } else {
                // 圖檔不存在：整段換成退化文字，而不是留一個空白圖片框
                QTextCharFormat plain;
                plain.setFontItalic(true);
                cur.removeSelectedText();
                cur.insertText(fx.missingLabel, plain);
            }
        }
        batch.endEditBlock();
    }

    /// 明確設定每個標題層級的字級。
    ///
    /// Qt 的 HTML 解析器對 h5/h6 會套用自己的 fontSizeAdjustment，
    /// stylesheet 裡的 font-size 蓋不掉 —— 實測 H5 會比正文還小、H6 又比 H5 大。
    /// 所以字級改在這裡設，Theme::headingPointSize() 是唯一定義處。
    void applyHeadingScale()
    {
        const Theme::Colors &c = Theme::colors(mode());

        struct Fix {
            int start;
            int end;
            QTextCharFormat fmt;
        };
        QVector<Fix> fixes;

        QTextDocument *doc = document();
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            const int level = b.blockFormat().headingLevel();
            if (level < 1 || level > 6)
                continue;

            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment f = it.fragment();
                if (!f.isValid() || f.charFormat().isImageFormat())
                    continue;

                // 從既有格式出發，只動需要動的，其餘（字族、行內 code 的底色…）保留
                QTextCharFormat fmt = f.charFormat();

                // 關鍵：這個屬性只要存在（即使是 0），Qt 就完全忽略 FontPointSize，
                // 改用「預設字級 × 層級係數」。實測 H1 設 23pt 卻畫成 18pt、
                // H5 設 11.5pt 卻畫成 7.2pt。必須清掉，設成 0 沒有用。
                fmt.clearProperty(QTextFormat::FontSizeAdjustment);
                fmt.setFontPointSize(Theme::headingPointSize(level) * m_zoom);
                fmt.setFontWeight(QFont::Bold);
                if (level == 6)
                    fmt.setForeground(QColor(c.muted));

                fixes.append({ f.position(), f.position() + f.length(), fmt });
            }
        }

        if (fixes.isEmpty())
            return;
        QTextCursor batch(doc);
        batch.beginEditBlock();
        for (int i = fixes.size() - 1; i >= 0; --i) {
            const Fix &fx = fixes.at(i);
            QTextCursor cur(doc);
            cur.setPosition(fx.start);
            cur.setPosition(fx.end, QTextCursor::KeepAnchor);
            // setCharFormat 而非 merge：merge 無法「移除」屬性
            cur.setCharFormat(fx.fmt);
        }
        batch.endEditBlock();
    }

    /// 表格改成「只有橫線」的樣式（對照 Chrome extension 的觀感）。
    ///
    /// 用 Qt 原生能力而不是自繪：QTextTableFormat::setBorderCollapse(true) 之後
    /// Qt 會渲染每個 cell 自己的邊框，所以把整體 border 設為 0、只給每列上框線
    /// 就得到橫線樣式。表頭那一列再加一條較粗的下框線。
    void applyTableStyling()
    {
        const Theme::Colors &c = Theme::colors(mode());
        const QBrush rule(QColor(c.border));

        QTextDocument *doc = document();
        QTextCursor batch(doc);
        batch.beginEditBlock();

        for (QTextFrame *frame : doc->rootFrame()->childFrames()) {
            auto *table = qobject_cast<QTextTable *>(frame);
            if (!table)
                continue;

            QTextTableFormat tf = table->format();
            tf.setBorder(0);
            tf.setBorderCollapse(true);   // 少了這行 Qt 不會畫 cell 層級的邊框
            tf.setCellSpacing(0);
            tf.setCellPadding(8);
            tf.setBorderBrush(rule);
            table->setFormat(tf);

            const int rows = table->rows();
            const int cols = table->columns();
            for (int r = 0; r < rows; ++r) {
                for (int col = 0; col < cols; ++col) {
                    QTextTableCell cell = table->cellAt(r, col);
                    if (!cell.isValid())
                        continue;
                    QTextTableCellFormat cf = cell.format().toTableCellFormat();

                    cf.setTopBorder(r == 0 ? 0 : 1);
                    cf.setTopBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                    cf.setTopBorderBrush(rule);

                    // 表頭下方那條線畫粗一點，把標題列分出來
                    const bool headerBottom = (r == 0 && rows > 1);
                    cf.setBottomBorder(headerBottom ? 2 : (r == rows - 1 ? 1 : 0));
                    cf.setBottomBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                    cf.setBottomBorderBrush(rule);

                    cf.setLeftBorder(0);
                    cf.setRightBorder(0);
                    cell.setFormat(cf);
                }
            }
        }
        batch.endEditBlock();
    }

    /// 文件中所有標題的位置，順序與 Document::toc 一致。
    QVector<int> headingPositions() const
    {
        QVector<int> out;
        const QTextDocument *doc = document();
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            if (b.blockFormat().headingLevel() > 0)
                out.append(b.position());
        }
        return out;
    }

protected:
    /// Qt rich-text 不支援 block 層級的 border，所以「標題底下的分隔線」與
    /// 「引用區塊左側色條」只能自己畫。
    ///
    /// 引用區塊的辨識靠 blockFormat().leftMargin() == Theme::BlockquoteIndentPx
    /// —— 那個值由 Theme 的 CSS 設定，是兩邊共用的契約（清單項目有 textList、
    /// 標題的 leftMargin 是 0，所以不會誤判）。
    void paintEvent(QPaintEvent *event) override
    {
        QTextBrowser::paintEvent(event);

        QTextDocument *doc = document();
        QAbstractTextDocumentLayout *layout = doc->documentLayout();
        if (!layout)
            return;

        const Theme::Colors &c = Theme::colors(mode());
        const QColor rule(c.border);
        const qreal margin = doc->documentMargin();
        const qreal dy = verticalScrollBar()->value();
        const qreal dx = horizontalScrollBar()->value();
        const QRectF clip = QRectF(event->rect()).adjusted(-2, -8, 2, 8);

        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing);

        // 從第一個可見的 block 開始，別每次重繪都掃全文件
        const int firstPos = layout->hitTest(QPointF(margin, dy), Qt::FuzzyHit);
        QTextBlock b = firstPos >= 0 ? doc->findBlock(firstPos) : doc->begin();
        if (!b.isValid())
            b = doc->begin();

        // 若第一個可見 block 位於引用區塊中間，要往回走到該段的起點，
        // 否則色條會從畫面上緣被切掉一截。
        while (b.isValid() && isBlockquote(b)) {
            const QTextBlock prev = b.previous();
            if (!prev.isValid() || !isBlockquote(prev))
                break;
            b = prev;
        }

        const qreal contentRight = qMax(margin + 40.0, viewport()->width() - margin);

        while (b.isValid()) {
            const QRectF r = layout->blockBoundingRect(b).translated(-dx, -dy);
            if (r.top() > clip.bottom())
                break;

            const QTextBlockFormat bf = b.blockFormat();

            // 標題分隔線：只有 H1 / H2，跟一般閱讀習慣一致
            const int level = bf.headingLevel();
            if ((level == 1 || level == 2) && !b.text().trimmed().isEmpty()) {
                if (r.bottom() >= clip.top()) {
                    p.setPen(QPen(rule, 1));
                    const qreal y = qRound(r.bottom() - 3.0) + 0.5;
                    p.drawLine(QPointF(margin, y), QPointF(contentRight, y));
                }
                b = b.next();
                continue;
            }

            // 引用區塊左色條：把連續的引用 block 併成一條，
            // 否則多段引用會畫成好幾截斷掉的短棒。
            if (isBlockquote(b)) {
                QRectF span = r;
                QTextBlock last = b;
                for (QTextBlock n = b.next(); n.isValid() && isBlockquote(n); n = n.next()) {
                    span = span.united(layout->blockBoundingRect(n).translated(-dx, -dy));
                    last = n;
                }
                if (span.bottom() >= clip.top() && span.top() <= clip.bottom()) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(rule);
                    const QRectF bar(margin + 2.0, span.top() + 2.0,
                                     qreal(Theme::BlockquoteBarPx),
                                     qMax(6.0, span.height() - 4.0));
                    p.drawRoundedRect(bar, Theme::BlockquoteBarPx / 2.0,
                                      Theme::BlockquoteBarPx / 2.0);
                }
                b = last.next();
                continue;
            }

            b = b.next();
        }
    }

    /// 引用區塊的辨識：leftMargin 等於 Theme 的契約值，且不是清單項目。
    static bool isBlockquote(const QTextBlock &b)
    {
        if (!b.isValid() || b.textList())
            return false;
        const QTextBlockFormat bf = b.blockFormat();
        if (bf.headingLevel() > 0)
            return false;
        return qFuzzyCompare(bf.leftMargin() + 1.0, qreal(Theme::BlockquoteIndentPx) + 1.0);
    }

    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type != int(QTextDocument::ImageResource))
            return QTextBrowser::loadResource(type, name);

        const QString url = name.toString();

        if (name.scheme() == QLatin1String("mermaid")) {
            const QString key = name.host().isEmpty() ? name.path() : name.host();
            const QString source = m_mermaidSources.value(key);
            const Theme::Colors &c = Theme::colors(m_dark ? Theme::Dark : Theme::Light);

            if (source.isEmpty()) {
                m_known.insert(url, true);
                m_logicalSize.insert(url, QSize(contentWidth(), 88));
                return placeholderImage(contentWidth(), QStringLiteral("[mermaid 區塊遺失]"), c);
            }
            if (!m_cache) {
                m_known.insert(url, true);
                m_logicalSize.insert(url, QSize(contentWidth(), 88));
                return placeholderImage(contentWidth(), QStringLiteral("[mermaid 未啟用]"), c);
            }
            if (m_cache->isCached(source, m_dark)) {
                QSize logical;
                const QString path = m_cache->pathFor(source, m_dark);
                QImage img = path.endsWith(QLatin1String(".svg"))
                                 ? rasterizeSvg(path, contentWidth(), &logical)
                                 : QImage(path);
                if (!img.isNull()) {
                    if (!logical.isValid() || logical.isEmpty()) {
                        // PNG 是 mmdc 以 N 倍光柵化的，顯示尺寸要除回去
                        const qreal scale = qMax(qreal(1.0), m_cache->outputScale());
                        logical = QSize(qRound(img.width() / scale),
                                        qRound(img.height() / scale));
                        if (logical.width() > contentWidth())
                            logical.scale(contentWidth(), INT_MAX, Qt::KeepAspectRatio);
                    }
                    m_logicalSize.insert(url, logical);
                    m_known.insert(url, true);
                    return img;
                }
                // 快取檔壞了 —— 當成未快取處理
            }

            m_cache->request(source, m_dark);
            m_known.insert(url, true);
            m_logicalSize.insert(url, QSize(contentWidth(), 88));
            return placeholderImage(contentWidth(), QStringLiteral("圖表產生中…"), c);
        }

        // 普通圖片
        QImage img;
        if (name.isLocalFile())
            img.load(name.toLocalFile());
        else
            img = qvariant_cast<QImage>(QTextBrowser::loadResource(type, name));

        if (!img.isNull())
            img = backdropIfLowContrast(img, QColor(Theme::colors(mode()).background));

        if (img.isNull()) {
            m_known.insert(url, false);
            m_logicalSize.remove(url);
            return {};
        }

        QSize logical = img.size();
        if (logical.width() > contentWidth())
            logical.scale(contentWidth(), INT_MAX, Qt::KeepAspectRatio);
        m_logicalSize.insert(url, logical);
        m_known.insert(url, true);
        return img;
    }

    void resizeEvent(QResizeEvent *e) override
    {
        QTextBrowser::resizeEvent(e);

        // 欄寬上限 + 置中。Qt rich-text 不支援 max-width，只能靠 viewport margin。
        const int avail = width();
        const int margin = qMax(0, (avail - Theme::ContentWidth) / 2);
        if (margin != m_appliedMargin) {
            m_appliedMargin = margin;
            setViewportMargins(margin, 0, margin, 0);
        }
    }

private:
    MermaidCache *m_cache = nullptr;
    QHash<QString, QString> m_mermaidSources;
    QHash<QString, QSize> m_logicalSize;
    QHash<QString, bool> m_known;       ///< url → 載入成功與否
    bool m_dark = false;
    qreal m_zoom = 1.0;
    int m_appliedMargin = -1;
};

// ---------------------------------------------------------------------------

TextBrowserBackend::TextBrowserBackend(MermaidCache *cache, QObject *parent)
    : IRenderBackend(parent)
    , m_cache(cache)
{
    m_view = new MdTextBrowser(cache);

    connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        Q_EMIT linkActivated(url);
    });
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this] { emitCurrentTocIndex(); });
}

TextBrowserBackend::~TextBrowserBackend()
{
    delete m_view;
}

QWidget *TextBrowserBackend::widget()
{
    return m_view;
}

void TextBrowserBackend::setDocument(const Document &doc)
{
    m_doc = doc;
    render(false);

    // 所有 mermaid 圖都排入渲染佇列（已快取的會被 request() 直接忽略）
    if (m_cache) {
        for (const MermaidBlock &b : m_doc.mermaid)
            m_cache->request(b.source, m_mode == Theme::Dark);
    }
}

void TextBrowserBackend::setTheme(Theme::Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    render(true);
    if (m_cache) {
        for (const MermaidBlock &b : m_doc.mermaid)
            m_cache->request(b.source, m_mode == Theme::Dark);
    }
}

void TextBrowserBackend::render(bool preserveScroll)
{
    const int scroll = preserveScroll ? scrollValue() : 0;
    const bool dark = (m_mode == Theme::Dark);

    m_view->setDark(dark);
    m_view->setMermaidSources(m_doc.mermaid);
    m_view->setPalette(Theme::palette(m_mode));
    m_view->document()->setDefaultStyleSheet(Theme::documentStyleSheet(m_mode));

    // 正文字級用 setDefaultFont 明確設定。
    // stylesheet 裡的 `body { font-size }` 實測**沒有生效** —— 內文一直是
    // widget 的系統預設字級（這台是 9pt），所以先前文件裡寫的「正文 11pt」
    // 是錯的。改用 setDefaultFont 才真的控制得到，縮放也才有單一施力點。
    QFont baseFont = m_view->document()->defaultFont();
    baseFont.setPointSizeF(Theme::BodyPointSize * m_zoom);
    m_view->document()->setDefaultFont(baseFont);
    m_view->setZoomFactor(m_zoom);

    // Qt 預設的縮排單位是 40px，巢狀清單與引用區塊在中文字體下會縮得很誇張。
    // Qt rich-text 不吃 CSS 的 margin-left/padding-left 來調清單縮排，
    // 唯一有效的旋鈕就是文件層級的 indentWidth。
    m_view->document()->setIndentWidth(20);

    if (!m_doc.baseDir.isEmpty())
        m_view->document()->setBaseUrl(QUrl::fromLocalFile(m_doc.baseDir + QLatin1Char('/')));

    m_view->setHtml(m_doc.html);
    m_view->applyImageSizing();
    m_view->applyHeadingScale();
    m_view->applyTableStyling();
    m_view->applyContrastFixups();

    m_view->verticalScrollBar()->setValue(scroll);
    m_lastTocIndex = -2;
    emitCurrentTocIndex();
}

void TextBrowserBackend::scrollToAnchor(const QString &anchor)
{
    m_view->scrollToAnchor(anchor);
}

int TextBrowserBackend::scrollValue() const
{
    return m_view->verticalScrollBar()->value();
}

void TextBrowserBackend::setScrollValue(int value)
{
    m_view->verticalScrollBar()->setValue(value);
}

void TextBrowserBackend::mermaidReady(const QString &key)
{
    Q_UNUSED(key);
    // 資源已在快取中，強制讓 QTextDocument 重新向 loadResource 取一次。
    const int scroll = scrollValue();
    m_view->setMermaidSources(m_doc.mermaid);
    m_view->setHtml(m_doc.html);
    m_view->applyImageSizing();
    m_view->applyHeadingScale();
    m_view->applyTableStyling();
    m_view->applyContrastFixups();
    setScrollValue(scroll);
}

void TextBrowserBackend::setZoom(qreal factor)
{
    const qreal wanted = qBound(MinZoom, factor, MaxZoom);
    if (qFuzzyCompare(m_zoom, wanted))
        return;
    m_zoom = wanted;
    // 重新套版而不是用 QTextEdit::zoomIn：後者只動 widget 字型，
    // 標題那些明確設定過 pointSize 的片段不會跟著變。
    render(true);
}

void TextBrowserBackend::zoomIn()
{
    setZoom(m_zoom * ZoomStep);
}

void TextBrowserBackend::zoomOut()
{
    setZoom(m_zoom / ZoomStep);
}

void TextBrowserBackend::resetZoom()
{
    setZoom(1.0);
}

void TextBrowserBackend::emitCurrentTocIndex()
{
    const QVector<int> positions = m_view->headingPositions();
    if (positions.isEmpty()) {
        if (m_lastTocIndex != -1) {
            m_lastTocIndex = -1;
            Q_EMIT currentTocIndexChanged(-1);
        }
        return;
    }

    const QTextCursor cur = m_view->cursorForPosition(QPoint(4, 4));
    const int pos = cur.position();

    int idx = -1;
    for (int i = 0; i < positions.size(); ++i) {
        if (positions.at(i) <= pos)
            idx = i;
        else
            break;
    }
    if (idx >= m_doc.toc.size())
        idx = m_doc.toc.size() - 1;

    if (idx != m_lastTocIndex) {
        m_lastTocIndex = idx;
        Q_EMIT currentTocIndexChanged(idx);
    }
}
