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
    , m_splitter(new QSplitter(Qt::Horizontal, this))
    , m_placeholder(new QLabel(this))
{
    m_splitter->setChildrenCollapsible(false);

    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->setText(QStringLiteral(
        "沒有開啟任何檔案\n\nCtrl+O 開啟　·　Ctrl+L 輸入路徑　·　或把 .md 拖進來"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_placeholder, 1);
    layout->addWidget(m_splitter, 1);

    createPane();
    updatePlaceholder();
}

// ------------------------------------------------------------------- 面板

int DocumentArea::paneCount() const
{
    return m_splitter->count();
}

PaneGroup *DocumentArea::paneAt(int index) const
{
    return qobject_cast<PaneGroup *>(m_splitter->widget(index));
}

PaneGroup *DocumentArea::createPane(int at)
{
    auto *pane = new PaneGroup(m_splitter);
    if (at < 0 || at >= m_splitter->count())
        m_splitter->addWidget(pane);
    else
        m_splitter->insertWidget(at, pane);
    wirePane(pane);
    if (!m_activeGroup)
        setActivePane(pane);
    return pane;
}

void DocumentArea::wirePane(PaneGroup *pane)
{
    connect(pane, &PaneGroup::activated, this, [this, pane] { setActivePane(pane); });
    connect(pane, &PaneGroup::currentChanged, this, [this, pane] {
        if (pane == m_activeGroup)
            Q_EMIT activeViewChanged();
    });
    connect(pane, &PaneGroup::tabsChanged, this, &DocumentArea::tabsChanged);
    connect(pane, &PaneGroup::closeRequested, this, [this, pane](int index) {
        setActivePane(pane);
        pane->removeView(index);
        pruneEmptyPanes();
        updatePlaceholder();
        Q_EMIT tabsChanged();
        Q_EMIT activeViewChanged();
    });
    connect(pane, &PaneGroup::tabDragOut, this, [this, pane](int index) {
        m_dragSource = { pane, index };
    });
    connect(pane, &PaneGroup::tabDropped, this, &DocumentArea::onTabDropped);
}

void DocumentArea::setActivePane(PaneGroup *pane)
{
    if (!pane || m_activeGroup == pane)
        return;
    m_activeGroup = pane;
    refreshPaneIndicators();
    Q_EMIT activeViewChanged();
}

void DocumentArea::pruneEmptyPanes()
{
    for (int i = m_splitter->count() - 1; i >= 0 && m_splitter->count() > 1; --i) {
        PaneGroup *pane = paneAt(i);
        if (!pane || pane->count() > 0)
            continue;
        if (pane == m_activeGroup)
            m_activeGroup = nullptr;
        pane->setParent(nullptr);
        pane->deleteLater();
    }
    if (!m_activeGroup)
        setActivePane(paneAt(0));

    refreshPaneIndicators();
}

void DocumentArea::refreshPaneIndicators()
{
    const bool multi = paneCount() > 1;
    for (int i = 0; i < m_splitter->count(); ++i)
        if (PaneGroup *p = paneAt(i))
            p->setActive(p == m_activeGroup, multi);
}

void DocumentArea::updatePlaceholder()
{
    const bool empty = count() == 0;
    m_placeholder->setVisible(empty);
    m_splitter->setVisible(!empty);
}

PaneGroup *DocumentArea::paneOf(DocumentView *view) const
{
    for (int i = 0; i < m_splitter->count(); ++i)
        if (PaneGroup *p = paneAt(i); p && p->indexOf(view) >= 0)
            return p;
    return nullptr;
}

void DocumentArea::setPaneCount(int panes)
{
    // 不做出空面板：面板數不會超過文件數
    const int wanted = qBound(1, qMin(panes, MaxPanes), qMax(1, count()));

    while (paneCount() < wanted) {
        // 從分頁最多的那一格勻一份出來，優先勻走「目前分頁的下一個」
        PaneGroup *donor = nullptr;
        for (int i = 0; i < m_splitter->count(); ++i)
            if (PaneGroup *p = paneAt(i); p && (!donor || p->count() > donor->count()))
                donor = p;
        if (!donor || donor->count() < 2)
            break;

        const int take = qMin(donor->currentIndex() + 1, donor->count() - 1);
        DocumentView *view = donor->takeView(take);
        PaneGroup *pane = createPane();
        view->setParent(nullptr);
        pane->setCurrentIndex(pane->addView(view));
    }

    while (paneCount() > wanted) {
        PaneGroup *last = paneAt(m_splitter->count() - 1);
        PaneGroup *prev = paneAt(m_splitter->count() - 2);
        if (!last || !prev)
            break;
        while (last->count() > 0) {
            DocumentView *view = last->takeView(0);
            view->setParent(nullptr);
            prev->addView(view);
        }
        if (last == m_activeGroup)
            m_activeGroup = nullptr;
        last->setParent(nullptr);
        last->deleteLater();
    }

    if (!m_activeGroup)
        setActivePane(paneAt(0));
    pruneEmptyPanes();
    refreshPaneIndicators();
    updatePlaceholder();
    Q_EMIT statusMessage(paneCount() == 1 ? QStringLiteral("單一面板")
                                          : QStringLiteral("已分割成 %1 格").arg(paneCount()));
    Q_EMIT tabsChanged();
    Q_EMIT activeViewChanged();
}

void DocumentArea::moveActiveTabToPane(int delta)
{
    PaneGroup *src = m_activeGroup;
    if (!src || src->count() == 0)
        return;

    const int here = m_splitter->indexOf(src);
    const int target = here + delta;

    DocumentView *view = src->takeView(src->currentIndex());
    if (!view)
        return;

    PaneGroup *dest = nullptr;
    if (target >= 0 && target < m_splitter->count())
        dest = paneAt(target);
    else if (paneCount() < MaxPanes)
        dest = createPane(delta > 0 ? -1 : 0);
    else
        dest = src;   // 已達上限，放回原處

    view->setParent(nullptr);
    dest->setCurrentIndex(dest->addView(view));
    setActivePane(dest);
    pruneEmptyPanes();
    updatePlaceholder();
    Q_EMIT tabsChanged();
    Q_EMIT activeViewChanged();
}

void DocumentArea::onTabDropped(PaneGroup *target, PaneGroup::DropZone zone)
{
    PaneGroup *src = m_dragSource.pane;
    const int index = m_dragSource.index;
    m_dragSource = {};
    moveTabToPane(src, index, target, zone);
}

void DocumentArea::moveTabToPane(PaneGroup *src, int index, PaneGroup *target,
                                 PaneGroup::DropZone zone)
{
    if (!src || !target || index < 0)
        return;

    // 放回自己身上的中間 = 什麼都不做
    if (zone == PaneGroup::DropZone::Into && src == target)
        return;
    // 這一格只有一個分頁，又要在它旁邊分割 = 等於沒變
    if (zone != PaneGroup::DropZone::Into && src == target && src->count() < 2)
        return;

    DocumentView *view = src->takeView(index);
    if (!view)
        return;

    PaneGroup *dest = target;
    if (zone != PaneGroup::DropZone::Into) {
        if (paneCount() >= MaxPanes) {
            dest = target;   // 已達面板上限，就併進目標格
        } else {
            const int at = m_splitter->indexOf(target)
                           + (zone == PaneGroup::DropZone::SplitRight ? 1 : 0);
            dest = createPane(at);
        }
    }

    view->setParent(nullptr);
    dest->setCurrentIndex(dest->addView(view));
    setActivePane(dest);
    pruneEmptyPanes();
    updatePlaceholder();
    Q_EMIT tabsChanged();
    Q_EMIT activeViewChanged();
}

// --------------------------------------------------------------- 全域索引

int DocumentArea::count() const
{
    int n = 0;
    for (int i = 0; i < m_splitter->count(); ++i)
        if (PaneGroup *p = paneAt(i))
            n += p->count();
    return n;
}

DocumentView *DocumentArea::viewAt(int index) const
{
    int seen = 0;
    for (int i = 0; i < m_splitter->count(); ++i) {
        PaneGroup *p = paneAt(i);
        if (!p)
            continue;
        if (index < seen + p->count())
            return p->viewAt(index - seen);
        seen += p->count();
    }
    return nullptr;
}

QStringList DocumentArea::openPaths() const
{
    QStringList out;
    for (int i = 0; i < m_splitter->count(); ++i)
        if (PaneGroup *p = paneAt(i))
            out << p->paths();
    return out;
}

int DocumentArea::activeIndex() const
{
    DocumentView *view = activeView();
    if (!view)
        return -1;
    for (int i = 0; i < count(); ++i)
        if (viewAt(i) == view)
            return i;
    return -1;
}

void DocumentArea::setActiveIndex(int index)
{
    DocumentView *view = viewAt(index);
    if (!view)
        return;
    PaneGroup *pane = paneOf(view);
    if (!pane)
        return;
    setActivePane(pane);
    pane->setCurrentIndex(pane->indexOf(view));
    Q_EMIT activeViewChanged();
}

DocumentView *DocumentArea::activeView() const
{
    return m_activeGroup ? m_activeGroup->currentView() : nullptr;
}

void DocumentArea::closeTab(int index)
{
    DocumentView *view = viewAt(index);
    if (!view)
        return;
    PaneGroup *pane = paneOf(view);
    if (!pane)
        return;
    pane->removeView(pane->indexOf(view));
    pruneEmptyPanes();
    updatePlaceholder();
    Q_EMIT tabsChanged();
    Q_EMIT activeViewChanged();
}

void DocumentArea::closeActiveTab()
{
    if (m_activeGroup && m_activeGroup->count() > 0) {
        m_activeGroup->removeView(m_activeGroup->currentIndex());
        pruneEmptyPanes();
        updatePlaceholder();
        Q_EMIT tabsChanged();
        Q_EMIT activeViewChanged();
    }
}

void DocumentArea::nextTab()
{
    const int n = count();
    if (n > 1)
        setActiveIndex((activeIndex() + 1) % n);
}

void DocumentArea::previousTab()
{
    const int n = count();
    if (n > 1)
        setActiveIndex((activeIndex() - 1 + n) % n);
}

QList<DocumentView *> DocumentArea::visibleViews() const
{
    QList<DocumentView *> out;
    for (int i = 0; i < m_splitter->count(); ++i)
        if (PaneGroup *p = paneAt(i); p && p->currentView())
            out << p->currentView();
    return out;
}

// ------------------------------------------------------------------- 開檔

DocumentView *DocumentArea::createView()
{
    auto *view = new DocumentView(m_cache, this);
    view->setTheme(m_mode);
    wireView(view);
    return view;
}

void DocumentArea::wireView(DocumentView *view)
{
    connect(view, &DocumentView::titleChanged, this, [this, view] {
        if (PaneGroup *p = paneOf(view))
            p->refreshTabText(view);
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
        if (PaneGroup *p = paneOf(view)) {
            setActivePane(p);
            p->setCurrentIndex(p->indexOf(view));
        }
        Q_EMIT linkActivated(url);
    });
    connect(view, &DocumentView::statusMessage, this, &DocumentArea::statusMessage);
}

DocumentView *DocumentArea::openFile(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();

    // 已經開過就切過去，不論它在哪一格
    for (int i = 0; i < m_splitter->count(); ++i) {
        PaneGroup *pane = paneAt(i);
        if (!pane)
            continue;
        const int at = pane->indexOfPath(abs);
        if (at >= 0) {
            setActivePane(pane);
            pane->setCurrentIndex(at);
            Q_EMIT activeViewChanged();
            return pane->viewAt(at);
        }
    }

    if (!m_activeGroup)
        setActivePane(paneAt(0));

    DocumentView *view = createView();

    // 先掛上分頁並讓它可見，**再**載入文件：隱藏的 widget 不會排版，
    // QTextDocument 的 loadResource 就不會被呼叫，圖片尺寸會全部變成 0。
    const int index = m_activeGroup->addView(view);
    m_activeGroup->setCurrentIndex(index);
    updatePlaceholder();

    if (!view->openFile(path)) {
        m_activeGroup->removeView(index);
        updatePlaceholder();
        return nullptr;
    }

    m_activeGroup->refreshTabText(view);
    Q_EMIT tabsChanged();
    Q_EMIT activeViewChanged();
    return view;
}

// ----------------------------------------------------------- 全域操作

void DocumentArea::setTheme(Theme::Mode mode)
{
    m_mode = mode;
    for (int i = 0; i < count(); ++i)
        if (DocumentView *v = viewAt(i))
            v->setTheme(mode);
}

void DocumentArea::zoomIn()
{
    // 縮放套用到全部分頁，分割時各格字級才會一致
    for (int i = 0; i < count(); ++i)
        if (DocumentView *v = viewAt(i))
            v->zoomIn();
}

void DocumentArea::zoomOut()
{
    for (int i = 0; i < count(); ++i)
        if (DocumentView *v = viewAt(i))
            v->zoomOut();
}

void DocumentArea::resetZoom()
{
    for (int i = 0; i < count(); ++i)
        if (DocumentView *v = viewAt(i))
            v->resetZoom();
}
