#include "DocumentView.h"

#include "core/FileWatcher.h"
#include "core/MarkdownParser.h"
#include "core/MermaidCache.h"
#include "render/TextBrowserBackend.h"

#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTextStream>
#include <QVBoxLayout>

DocumentView::DocumentView(MermaidCache *cache, QWidget *parent)
    : QWidget(parent)
    , m_cache(cache)
    , m_watcher(new FileWatcher(this))
{
    auto *backend = new TextBrowserBackend(m_cache, this);
    m_backend = backend;

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_backend->widget());

    // 拖放交給 MainWindow 處理。這裡必須自己關掉 —— 分頁是動態建立的，
    // MainWindow 在建構時遍歷子 widget 抓不到之後才出現的 view。
    setAcceptDrops(false);
    m_backend->widget()->setAcceptDrops(false);

    connect(m_backend, &IRenderBackend::linkActivated,
            this, &DocumentView::linkActivated);
    connect(m_backend, &IRenderBackend::currentTocIndexChanged,
            this, &DocumentView::currentTocIndexChanged);

    connect(m_watcher, &FileWatcher::fileChanged, this, [this] {
        QFile f(m_path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        m_markdown = in.readAll();
        reparse(true);
        Q_EMIT statusMessage(QStringLiteral("已重新載入 ") + QFileInfo(m_path).fileName());
    });

    if (m_cache) {
        connect(m_cache, &MermaidCache::rendered, this, [this](const QString &key, const QString &) {
            // 每個 view 都會收到，但只有真的用到那張圖的才需要重畫
            for (const MermaidBlock &b : m_doc.mermaid) {
                if (m_cache->keyFor(b.source, m_mode == Theme::Dark) == key) {
                    m_backend->mermaidReady(key);
                    return;
                }
            }
        });
    }
}

DocumentView::~DocumentView() = default;

QString DocumentView::title() const
{
    if (m_path.isEmpty())
        return {};
    return m_doc.title.isEmpty() ? QFileInfo(m_path).fileName() : m_doc.title;
}

bool DocumentView::openFile(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.isFile()) {
        Q_EMIT statusMessage(QStringLiteral("找不到檔案: ") + path);
        return false;
    }

    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("開啟失敗"),
                             QStringLiteral("無法讀取 %1\n%2").arg(path, f.errorString()));
        return false;
    }

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    m_markdown = in.readAll();
    m_path = fi.absoluteFilePath();

    reparse(false);
    m_watcher->watch(m_path);
    return true;
}

void DocumentView::reparse(bool preserveScroll)
{
    MarkdownParser::Options opt;
    // mmdc 不在就別產生佔位圖，直接把 mermaid 當程式碼區塊顯示
    opt.mermaidEnabled = m_cache && m_cache->rendererAvailable();
    opt.darkTheme = (m_mode == Theme::Dark);

    const int scroll = preserveScroll ? scrollValue() : 0;
    const QString oldTitle = title();

    m_doc = MarkdownParser::parse(m_markdown, QFileInfo(m_path).absolutePath(), opt);
    m_backend->setTheme(m_mode);
    m_backend->setDocument(m_doc);

    if (preserveScroll)
        setScrollValue(scroll);

    Q_EMIT documentReplaced();
    if (title() != oldTitle)
        Q_EMIT titleChanged();

    if (!m_doc.mermaid.isEmpty() && m_cache && !m_cache->rendererAvailable()
        && !m_degradeNoticeShown) {
        m_degradeNoticeShown = true;
        Q_EMIT statusMessage(QStringLiteral("未找到 mmdc，mermaid 以原始碼顯示。"
                                            "安裝: npm i -g @mermaid-js/mermaid-cli"));
    }
}

void DocumentView::setTheme(Theme::Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;

    // 語法高亮的配色是在產 HTML 時決定的，所以主題切換必須重新解析
    if (!m_markdown.isEmpty())
        reparse(true);
    else
        m_backend->setTheme(m_mode);
}

void DocumentView::scrollToAnchor(const QString &anchor) { m_backend->scrollToAnchor(anchor); }
int DocumentView::scrollValue() const { return m_backend->scrollValue(); }
void DocumentView::setScrollValue(int value) { m_backend->setScrollValue(value); }
void DocumentView::zoomIn() { m_backend->zoomIn(); }
void DocumentView::zoomOut() { m_backend->zoomOut(); }
void DocumentView::resetZoom() { m_backend->resetZoom(); }
