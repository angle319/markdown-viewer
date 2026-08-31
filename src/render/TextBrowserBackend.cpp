#include "render/TextBrowserBackend.h"

#include "core/MermaidCache.h"

#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSvgRenderer>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>

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
    }

    void setMermaidSources(const QVector<MermaidBlock> &blocks)
    {
        m_mermaidSources.clear();
        for (const MermaidBlock &b : blocks)
            m_mermaidSources.insert(b.key, b.source);
    }

    void setDark(bool dark) { m_dark = dark; }

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

        // 收集完再改，避免邊走邊改文件
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

    // Qt 預設的縮排單位是 40px，巢狀清單與引用區塊在中文字體下會縮得很誇張。
    // Qt rich-text 不吃 CSS 的 margin-left/padding-left 來調清單縮排，
    // 唯一有效的旋鈕就是文件層級的 indentWidth。
    m_view->document()->setIndentWidth(20);

    if (!m_doc.baseDir.isEmpty())
        m_view->document()->setBaseUrl(QUrl::fromLocalFile(m_doc.baseDir + QLatin1Char('/')));

    m_view->setHtml(m_doc.html);
    m_view->applyImageSizing();

    if (m_zoomSteps != 0) {
        if (m_zoomSteps > 0)
            m_view->zoomIn(m_zoomSteps);
        else
            m_view->zoomOut(-m_zoomSteps);
    }

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
    setScrollValue(scroll);
}

void TextBrowserBackend::zoomIn()
{
    ++m_zoomSteps;
    m_view->zoomIn(1);
}

void TextBrowserBackend::zoomOut()
{
    --m_zoomSteps;
    m_view->zoomOut(1);
}

void TextBrowserBackend::resetZoom()
{
    if (m_zoomSteps > 0)
        m_view->zoomOut(m_zoomSteps);
    else if (m_zoomSteps < 0)
        m_view->zoomIn(-m_zoomSteps);
    m_zoomSteps = 0;
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
