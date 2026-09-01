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

/// SVG is rasterised at 2x and the display size is then set explicitly on the
/// image format. That keeps it sharp on HiDPI and makes the size fully
/// controlled rather than relying on Qt's guess at the SVG's intrinsic size.
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

/// An image with a transparent background whose content is close in colour to
/// the page is effectively invisible on that theme. This measures the mean
/// luminance of the visible pixels and, when contrast is too low, composites the
/// image onto a neutral card with padding.
/// Mermaid diagrams skip this: their theme is chosen here and already matches.
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
        return img;   // Fully transparent; nothing to fix and nothing to break

    const double meanLum = sum / n;
    // Build a representative grey from the mean luminance for the contrast
    // calculation (undo the sRGB gamma)
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
// MdTextBrowser: overrides resource loading and centres the content column.
// Deliberately no Q_OBJECT — it only overrides virtuals and needs no signals
// or slots of its own.
// ---------------------------------------------------------------------------
class MdTextBrowser : public QTextBrowser
{
public:
    explicit MdTextBrowser(MermaidCache *cache, QWidget *parent = nullptr)
        : QTextBrowser(parent)
        , m_cache(cache)
    {
        setOpenLinks(false);           // MainWindow decides what a link does
        setOpenExternalLinks(false);
        setFrameShape(QFrame::NoFrame);

        // A viewer needs no undo, and the tree walks below make hundreds of
        // format changes, every one of which would be pushed onto the undo
        // stack. Turning it off saves a great deal of time and memory.
        document()->setUndoRedoEnabled(false);
    }

    void setMermaidSources(const QVector<MermaidBlock> &blocks)
    {
        m_mermaidSources.clear();
        for (const MermaidBlock &b : blocks)
            m_mermaidSources.insert(b.key, b.source);
    }

    void setDark(bool dark) { m_dark = dark; }

    /// Zoom factor. Heading sizes are set explicitly (see applyHeadingScale)
    /// and therefore do not follow the widget font, so the factor has to be
    /// handed to them as well.
    void setZoomFactor(qreal factor) { m_zoom = factor; }
    qreal zoomFactor() const { return m_zoom; }
    Theme::Mode mode() const { return m_dark ? Theme::Dark : Theme::Light; }

    /// Guarantees there is no invisible text.
    ///
    /// Markdown may embed raw HTML with hard-coded colours
    /// (`<span style="color:#000">`), which is invisible on the black theme.
    /// This measures each text fragment's WCAG contrast against its *effective*
    /// background and, below 4.5:1, replaces the foreground with one that is
    /// readable there.
    /// Effective background: the fragment's own, else its block's, else the page.
    ///
    /// 中：救的是 markdown 內嵌原始 HTML 寫死顏色的情況。
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
            // merge rather than set: only the foreground changes, so font,
            // weight and the rest survive
            QTextCharFormat merge;
            merge.setForeground(fx.fg);
            cur.mergeCharFormat(merge);
        }
        batch.endEditBlock();
    }

    /// Logical (display) size of an image, recorded by loadResource for
    /// applyImageSizing to use.
    QSize logicalSize(const QString &url) const { return m_logicalSize.value(url); }

    int contentWidth() const
    {
        return qMin(Theme::ContentWidth, qMax(240, viewport()->width() - 32));
    }

    /// Okular's markdown converter also walks the document after building it to
    /// fix image sizes. As well as applying the logical size, this handles the
    /// degraded display for an image file that does not exist.
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

                // If layout has not run yet (the widget was hidden when the
                // document loaded, say), loadResource was never called; ask for
                // the resource explicitly so the size is known.
                if (!m_logicalSize.contains(url) && !m_known.contains(url))
                    doc->resource(QTextDocument::ImageResource, QUrl(url));

                const QSize logical = m_logicalSize.value(url);

                if (logical.isValid() && !logical.isEmpty()) {
                    fmt.setWidth(logical.width());
                    fmt.setHeight(logical.height());
                    fixes.append({ f.position(), f.position() + f.length(), fmt, QString() });
                } else if (m_known.contains(url) && !m_known.value(url)) {
                    // Resource failed to load: fall back to naming the file
                    // (QTextImageFormat does not keep the alt text)
                    const QUrl u(url);
                    const QString name = u.isLocalFile()
                                             ? QFileInfo(u.toLocalFile()).fileName()
                                             : url;
                    fixes.append({ f.position(), f.position() + f.length(), fmt,
                                   QStringLiteral("[缺少圖片: %1]").arg(name) });
                }
            }
        }

        // Collect first, then mutate, so the document is not edited while being
        // walked. Wrap the batch in one edit block: otherwise every
        // setCharFormat triggers a full re-layout, which is quadratic in the
        // number of fragments.
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
                // Missing file: replace the whole fragment with text rather
                // than leaving an empty image box
                QTextCharFormat plain;
                plain.setFontItalic(true);
                cur.removeSelectedText();
                cur.insertText(fx.missingLabel, plain);
            }
        }
        batch.endEditBlock();
    }

    /// Sets the point size for every heading level explicitly.
    ///
    /// Qt's HTML parser applies its own fontSizeAdjustment to h5/h6, which the
    /// stylesheet's font-size cannot override — measured, H5 came out smaller
    /// than body text and H6 larger than H5. Sizes are therefore set here, with
    /// Theme::headingPointSize() as the single definition.
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

                // Start from the existing format and change only what must
                // change, so families and inline-code chips survive
                QTextCharFormat fmt = f.charFormat();

                // The crucial part: while this property is present — even set
                // to 0 — Qt ignores FontPointSize entirely and uses
                // "default size x level factor" instead. Measured, H1 set to
                // 23pt drew at 18pt and H5 set to 11.5pt drew at 7.2pt.
                // It must be cleared; setting it to 0 does nothing.
                //
                // 中：這個屬性存在就會蓋掉字級，設 0 沒用，必須 clear。
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
            // setCharFormat rather than merge: merge cannot remove a property
            cur.setCharFormat(fx.fmt);
        }
        batch.endEditBlock();
    }

    /// Styles tables with horizontal rules only, matching the look of the
    /// Chrome extension this was compared against.
    ///
    /// This uses Qt's native cell borders rather than custom painting: with
    /// QTextTableFormat::setBorderCollapse(true) Qt renders each cell's own
    /// borders, so setting the overall border to 0 and giving every row a top
    /// border produces horizontal rules. The header row gets a heavier bottom
    /// border.
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
            tf.setBorderCollapse(true);   // Without this Qt draws no cell borders at all
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

                    // A heavier rule under the header separates it
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

    /// Positions of every heading, in the same order as Document::toc.
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
    /// Qt rich text has no block-level border, so the rule under a heading and
    /// the blockquote bar have to be painted by hand.
    ///
    /// Blockquotes are identified by
    /// blockFormat().leftMargin() == Theme::BlockquoteIndentPx — a value set by
    /// Theme's stylesheet, making it a contract shared by both sides. List items
    /// are excluded because they have a textList() and headings because their
    /// left margin is 0.
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

        // Start from the first visible block; do not scan the whole document
        // on every repaint
        const int firstPos = layout->hitTest(QPointF(margin, dy), Qt::FuzzyHit);
        QTextBlock b = firstPos >= 0 ? doc->findBlock(firstPos) : doc->begin();
        if (!b.isValid())
            b = doc->begin();

        // If the first visible block sits in the middle of a blockquote, walk
        // back to the start of it, or the bar is clipped at the top edge.
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

            // Heading rules for H1 and H2 only, as most readers expect
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

            // Blockquote bar: merge consecutive blockquote blocks into one bar,
            // otherwise a multi-paragraph quote gets several broken stubs.
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

    /// A blockquote is a block whose leftMargin matches Theme's contract value
    /// and which is not a list item.
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
                        // PNG is rasterised by mmdc at N x, so divide back down
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
                // The cache file is broken; treat it as not cached
            }

            m_cache->request(source, m_dark);
            m_known.insert(url, true);
            m_logicalSize.insert(url, QSize(contentWidth(), 88));
            return placeholderImage(contentWidth(), QStringLiteral("圖表產生中…"), c);
        }

        // An ordinary image
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

        // Cap and centre the content column. Qt rich text has no max-width, so
        // viewport margins are the only lever.
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
    QHash<QString, bool> m_known;       ///< url -> whether it loaded
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

    // Queue every mermaid diagram; request() ignores the ones already cached
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

    // Body point size is set explicitly with setDefaultFont.
    // Measured, the stylesheet's `body { font-size }` has **no effect at all**:
    // body text stayed at the widget's system default (9pt on this machine),
    // which made the "11pt body" claim in earlier docs wrong. setDefaultFont is
    // what actually controls it, and it gives zoom a single place to act.
    QFont baseFont = m_view->document()->defaultFont();
    baseFont.setPointSizeF(Theme::BodyPointSize * m_zoom);
    m_view->document()->setDefaultFont(baseFont);
    m_view->setZoomFactor(m_zoom);

    // Qt's default indent unit is 40px, which makes nested lists and
    // blockquotes look wildly over-indented with CJK text. Qt rich text does not
    // honour CSS margin-left/padding-left for list indentation, so the
    // document-level indentWidth is the only lever that works.
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
    // The resource is now cached; force QTextDocument to ask loadResource again.
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
    // Re-render rather than calling QTextEdit::zoomIn: that only changes the
    // widget font, so fragments with an explicit point size — every heading —
    // would not follow.
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
