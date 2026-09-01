#include "DocumentArea.h"

#include "DocumentView.h"

#include <QFileInfo>
#include <QLabel>
#include <QSplitter>
#include <QTabBar>
#include <QVBoxLayout>

DocumentArea::DocumentArea(MermaidCache *cache, QWidget *parent)
    : QWidget(parent)
    , m_cache(cache)
    , m_tabBar(new QTabBar(this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
    , m_placeholder(new QLabel(this))
{
    m_tabBar->setTabsClosable(true);
    m_tabBar->setMovable(true);           // 拖曳排序即可換比較對象
    m_tabBar->setExpanding(false);
    m_tabBar->setElideMode(Qt::ElideMiddle);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setDrawBase(false);

    m_splitter->setChildrenCollapsible(false);

    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->setText(QStringLiteral(
        "沒有開啟任何檔案\n\nCtrl+O 開啟　·　Ctrl+L 輸入路徑　·　或把 .md 拖進來"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabBar);
    layout->addWidget(m_placeholder, 1);
    layout->addWidget(m_splitter, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int) {
        updateVisibility();
        Q_EMIT activeViewChanged();
    });
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &DocumentArea::closeTab);
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int, int) {
        // tabData 跟著 tab 一起移動，所以順序自動就是對的
        updateVisibility();
        Q_EMIT tabsChanged();
    });

    updateVisibility();
}

int DocumentArea::count() const
{
    return m_tabBar->count();
}

DocumentView *DocumentArea::viewAt(int index) const
{
    if (index < 0 || index >= m_tabBar->count())
        return nullptr;
    return m_tabBar->tabData(index).value<DocumentView *>();
}

DocumentView *DocumentArea::activeView() const
{
    return viewAt(m_tabBar->currentIndex());
}

int DocumentArea::activeIndex() const
{
    return m_tabBar->currentIndex();
}

void DocumentArea::setActiveIndex(int index)
{
    if (index >= 0 && index < m_tabBar->count())
        m_tabBar->setCurrentIndex(index);
}

QStringList DocumentArea::openPaths() const
{
    QStringList out;
    for (int i = 0; i < m_tabBar->count(); ++i)
        if (DocumentView *v = viewAt(i))
            out << v->path();
    return out;
}

int DocumentArea::indexOf(DocumentView *view) const
{
    for (int i = 0; i < m_tabBar->count(); ++i)
        if (viewAt(i) == view)
            return i;
    return -1;
}

int DocumentArea::indexOfPath(const QString &path) const
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_tabBar->count(); ++i)
        if (DocumentView *v = viewAt(i); v && v->path() == abs)
            return i;
    return -1;
}

DocumentView *DocumentArea::createView()
{
    auto *view = new DocumentView(m_cache, m_splitter);
    view->setTheme(m_mode);
    view->hide();
    m_splitter->addWidget(view);
    wire(view);
    return view;
}

void DocumentArea::wire(DocumentView *view)
{
    connect(view, &DocumentView::titleChanged, this, [this, view] {
        updateTabText(view);
        if (view == activeView())
            Q_EMIT activeViewChanged();
    });
    connect(view, &DocumentView::documentReplaced, this, [this, view] {
        if (view == activeView())
            Q_EMIT activeDocumentChanged();
    });
    connect(view, &DocumentView::currentTocIndexChanged, this, [this, view](int i) {
        if (view == activeView())
            Q_EMIT currentTocIndexChanged(i);
    });
    connect(view, &DocumentView::linkActivated, this, [this, view](const QUrl &url) {
        // 連結一律在觸發它的那個分頁裡導航
        if (view != activeView())
            setActiveIndex(indexOf(view));
        Q_EMIT linkActivated(url);
    });
    connect(view, &DocumentView::statusMessage, this, &DocumentArea::statusMessage);
}

void DocumentArea::updateTabText(DocumentView *view)
{
    const int i = indexOf(view);
    if (i < 0)
        return;
    const QString title = view->title();
    m_tabBar->setTabText(i, title.isEmpty() ? QStringLiteral("(未命名)") : title);
    m_tabBar->setTabToolTip(i, view->path());
}

DocumentView *DocumentArea::openFile(const QString &path)
{
    const int existing = indexOfPath(path);
    if (existing >= 0) {
        setActiveIndex(existing);
        return viewAt(existing);
    }

    DocumentView *view = createView();

    // 先掛上分頁並讓它可見，**再**載入文件：隱藏的 widget 不會排版，
    // QTextDocument 的 loadResource 就不會被呼叫，圖片尺寸會全部變成 0。
    const int index = m_tabBar->addTab(QFileInfo(path).fileName());
    m_tabBar->setTabData(index, QVariant::fromValue(view));
    m_tabBar->setCurrentIndex(index);
    updateVisibility();

    if (!view->openFile(path)) {
        m_tabBar->removeTab(index);
        view->setParent(nullptr);
        view->deleteLater();
        updateVisibility();
        return nullptr;
    }

    updateTabText(view);
    Q_EMIT tabsChanged();
    Q_EMIT activeViewChanged();
    return view;
}

void DocumentArea::closeTab(int index)
{
    DocumentView *view = viewAt(index);
    if (!view)
        return;

    m_tabBar->removeTab(index);
    view->setParent(nullptr);
    view->deleteLater();

    updateVisibility();
    Q_EMIT tabsChanged();
    Q_EMIT activeViewChanged();
}

void DocumentArea::closeActiveTab()
{
    closeTab(m_tabBar->currentIndex());
}

void DocumentArea::nextTab()
{
    if (m_tabBar->count() > 1)
        m_tabBar->setCurrentIndex((m_tabBar->currentIndex() + 1) % m_tabBar->count());
}

void DocumentArea::previousTab()
{
    if (m_tabBar->count() > 1)
        m_tabBar->setCurrentIndex(
            (m_tabBar->currentIndex() - 1 + m_tabBar->count()) % m_tabBar->count());
}

void DocumentArea::setCompareColumns(int columns)
{
    const int wanted = qBound(1, columns, MaxCompareColumns);
    if (m_compareColumns == wanted)
        return;
    m_compareColumns = wanted;
    updateVisibility();
    Q_EMIT statusMessage(wanted == 1
                             ? QStringLiteral("已離開比較模式")
                             : QStringLiteral("比較模式：%1 欄").arg(wanted));
}

QList<DocumentView *> DocumentArea::visibleViews() const
{
    QList<DocumentView *> out;
    for (int i = 0; i < m_tabBar->count(); ++i)
        if (DocumentView *v = viewAt(i); v && v->isVisible())
            out << v;
    return out;
}

void DocumentArea::updateVisibility()
{
    const int n = m_tabBar->count();
    m_placeholder->setVisible(n == 0);
    m_splitter->setVisible(n > 0);
    if (n == 0)
        return;

    const int columns = qBound(1, m_compareColumns, n);
    // 從目前分頁起算連續 columns 個；右邊不夠就把視窗往左滑，確保滿欄
    const int start = qBound(0, qMin(m_tabBar->currentIndex(), n - columns), n - columns);

    for (int i = 0; i < n; ++i)
        if (DocumentView *v = viewAt(i))
            v->setVisible(i >= start && i < start + columns);
}

void DocumentArea::setTheme(Theme::Mode mode)
{
    m_mode = mode;
    for (int i = 0; i < m_tabBar->count(); ++i)
        if (DocumentView *v = viewAt(i))
            v->setTheme(mode);
}

void DocumentArea::zoomIn()
{
    // 縮放套用到全部分頁，比較模式下各欄字級才會一致
    for (int i = 0; i < m_tabBar->count(); ++i)
        if (DocumentView *v = viewAt(i))
            v->zoomIn();
}

void DocumentArea::zoomOut()
{
    for (int i = 0; i < m_tabBar->count(); ++i)
        if (DocumentView *v = viewAt(i))
            v->zoomOut();
}

void DocumentArea::resetZoom()
{
    for (int i = 0; i < m_tabBar->count(); ++i)
        if (DocumentView *v = viewAt(i))
            v->resetZoom();
}
