#include "MainWindow.h"

#include "DocumentArea.h"
#include "DocumentView.h"
#include "FileBrowserPanel.h"
#include "PathBar.h"
#include "Sidebar.h"
#include "TocPanel.h"
#include "core/MermaidCache.h"
#include "core/MmdcRenderer.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QtMath>

namespace {

const QStringList kMarkdownSuffixes{ QStringLiteral("md"),  QStringLiteral("markdown"),
                                     QStringLiteral("mdx"), QStringLiteral("mdc"),
                                     QStringLiteral("mkd"), QStringLiteral("txt") };

bool looksLikeMarkdown(const QString &path)
{
    return kMarkdownSuffixes.contains(QFileInfo(path).suffix().toLower());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_renderer = new MmdcRenderer(this);
    // Rasterisation scale follows the display: rendering at 2x on a 1x screen
    // wastes memory for no visible gain (measured, about 10 MB of PSS for the
    // two diagrams in sample.md).
    m_renderer->setPngScale(qMax(1, qCeil(qApp->devicePixelRatio())));
    m_cache = new MermaidCache(m_renderer, this);

    buildUi();
    buildMenus();
    loadSettings();
    updateStatus();
}

MainWindow::~MainWindow() = default;

DocumentView *MainWindow::activeView() const
{
    return m_area ? m_area->activeView() : nullptr;
}

void MainWindow::buildUi()
{
    m_sidebar = new Sidebar(this);
    m_area = new DocumentArea(m_cache, this);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_area);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setChildrenCollapsible(false);

    m_pathBar = new PathBar(this);

    auto *central = new QWidget(this);
    auto *box = new QVBoxLayout(central);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(0);
    box->addWidget(m_pathBar);
    box->addWidget(m_splitter, 1);
    setCentralWidget(central);

    m_statusRight = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusRight);

    connect(m_pathBar, &PathBar::pathSubmitted, this, &MainWindow::onPathSubmitted);
    connect(m_pathBar, &PathBar::cancelled, this, [this] {
        if (DocumentView *v = activeView())
            v->setFocus(Qt::OtherFocusReason);
    });

    connect(m_sidebar->toc(), &TocPanel::anchorActivated, this, [this](const QString &a) {
        if (DocumentView *v = activeView())
            v->scrollToAnchor(a);
    });
    connect(m_sidebar->files(), &FileBrowserPanel::fileActivated,
            this, [this](const QString &p) { openFile(p); });

    connect(m_area, &DocumentArea::activeViewChanged, this, &MainWindow::syncToActiveView);
    connect(m_area, &DocumentArea::activeDocumentChanged, this, &MainWindow::syncToActiveView);
    connect(m_area, &DocumentArea::currentTocIndexChanged,
            m_sidebar->toc(), &TocPanel::highlightIndex);
    connect(m_area, &DocumentArea::linkActivated, this, &MainWindow::onLinkActivated);
    connect(m_area, &DocumentArea::statusMessage, this,
            [this](const QString &t) { updateStatus(t); });
    connect(m_area, &DocumentArea::tabsChanged, this, [this] { updateStatus(); });

    connect(m_cache, &MermaidCache::failed, this, [this](const QString &, const QString &err) {
        updateStatus(QStringLiteral("mermaid 渲染失敗: ") + err.left(160));
    });
    connect(m_cache, &MermaidCache::idle, this, [this] { updateStatus(); });

    resize(1200, 820);

    // Drag and drop to open. QTextBrowser and the sidebar views swallow drop
    // events by default, so turn their acceptDrops off and let the event bubble
    // up to MainWindow.
    setAcceptDrops(true);
    m_area->setAcceptDrops(false);
    m_sidebar->setAcceptDrops(false);
    for (QWidget *w : m_sidebar->findChildren<QWidget *>())
        w->setAcceptDrops(false);
}

QString MainWindow::firstUsablePath(const QList<QUrl> &urls)
{
    // Prefer a markdown file, falling back to a directory. When several items
    // are dropped at once, opening a document is the more expected outcome.
    QString folder;
    for (const QUrl &u : urls) {
        if (!u.isLocalFile())
            continue;
        const QString path = u.toLocalFile();
        const QFileInfo fi(path);
        if (fi.isFile() && looksLikeMarkdown(path))
            return fi.absoluteFilePath();
        if (fi.isDir() && folder.isEmpty())
            folder = fi.absoluteFilePath();
    }
    return folder;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() && !firstUsablePath(e->mimeData()->urls()).isEmpty())
        e->acceptProposedAction();
    else
        e->ignore();
}

bool MainWindow::openFromUrls(const QList<QUrl> &urls)
{
    const QString path = firstUsablePath(urls);
    if (path.isEmpty()) {
        updateStatus(QStringLiteral("拖入的東西不是 markdown 檔或資料夾"));
        return false;
    }
    onPathSubmitted(path);   // Same route as the path bar: open a file, re-root
                             // on a directory
    return true;
}

void MainWindow::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasUrls() && openFromUrls(e->mimeData()->urls()))
        e->acceptProposedAction();
    else
        e->ignore();
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("檔案(&F)"));

    auto *actOpen = fileMenu->addAction(QStringLiteral("開啟…"));
    actOpen->setShortcut(QKeySequence::Open);
    connect(actOpen, &QAction::triggered, this, &MainWindow::onOpenTriggered);

    auto *actReload = fileMenu->addAction(QStringLiteral("重新載入"));
    actReload->setShortcut(QKeySequence::Refresh);
    connect(actReload, &QAction::triggered, this, &MainWindow::onReloadTriggered);

    fileMenu->addSeparator();

    auto *actCloseTab = fileMenu->addAction(QStringLiteral("關閉分頁"));
    actCloseTab->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    connect(actCloseTab, &QAction::triggered, this, [this] { m_area->closeActiveTab(); });
    addAction(actCloseTab);

    auto *actNext = fileMenu->addAction(QStringLiteral("下一個分頁"));
    actNext->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_Tab),
                            QKeySequence(Qt::CTRL | Qt::Key_PageDown) });
    connect(actNext, &QAction::triggered, this, [this] { m_area->nextTab(); });
    addAction(actNext);

    auto *actPrev = fileMenu->addAction(QStringLiteral("上一個分頁"));
    actPrev->setShortcuts({ QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
                            QKeySequence(Qt::CTRL | Qt::Key_PageUp) });
    connect(actPrev, &QAction::triggered, this, [this] { m_area->previousTab(); });
    addAction(actPrev);

    // Alt+1..9 jumps straight to a tab (Alt+Shift+1/2 are the themes, no clash)
    for (int i = 1; i <= 9; ++i) {
        auto *jump = new QAction(this);
        jump->setShortcut(QKeySequence(Qt::ALT | Qt::Key(Qt::Key_0 + i)));
        connect(jump, &QAction::triggered, this, [this, i] { m_area->setActiveIndex(i - 1); });
        addAction(jump);
    }

    fileMenu->addSeparator();
    auto *actQuit = fileMenu->addAction(QStringLiteral("結束"));
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("檢視(&V)"));

    m_actSidebar = viewMenu->addAction(QStringLiteral("顯示側邊欄"));
    m_actSidebar->setCheckable(true);
    m_actSidebar->setChecked(true);
    m_actSidebar->setShortcut(QKeySequence(Qt::Key_F9));
    connect(m_actSidebar, &QAction::toggled, this,
            [this](bool on) { m_sidebar->setVisible(on); });

    viewMenu->addSeparator();
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    m_actWhite = viewMenu->addAction(Theme::name(Theme::Light));
    m_actWhite->setCheckable(true);
    m_actWhite->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_1));
    themeGroup->addAction(m_actWhite);
    connect(m_actWhite, &QAction::triggered, this, [this] { setMode(Theme::Light); });

    m_actBlack = viewMenu->addAction(Theme::name(Theme::Dark));
    m_actBlack->setCheckable(true);
    m_actBlack->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_2));
    themeGroup->addAction(m_actBlack);
    connect(m_actBlack, &QAction::triggered, this, [this] { setMode(Theme::Dark); });

    auto *actToggleTheme = viewMenu->addAction(QStringLiteral("切換主題"));
    actToggleTheme->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_T));
    connect(actToggleTheme, &QAction::triggered, this,
            [this] { setMode(m_mode == Theme::Dark ? Theme::Light : Theme::Dark); });

    // ---- Split panes ----
    viewMenu->addSeparator();
    m_compareGroup = new QActionGroup(this);
    m_compareGroup->setExclusive(true);

    struct CompareItem {
        const char *text;
        int columns;
        Qt::Key key;
    };
    static const CompareItem items[] = {
        { "單一面板（不分割）", 1, Qt::Key_1 },
        { "分割 2 格", 2, Qt::Key_2 },
        { "分割 3 格", 3, Qt::Key_3 },
        { "分割 4 格", 4, Qt::Key_4 },
    };
    for (const CompareItem &item : items) {
        auto *act = viewMenu->addAction(QString::fromUtf8(item.text));
        act->setCheckable(true);
        act->setChecked(item.columns == 1);
        act->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | item.key));
        act->setData(item.columns);
        m_compareGroup->addAction(act);
        connect(act, &QAction::triggered, this,
                [this, cols = item.columns] { setCompareColumns(cols); });
        addAction(act);
    }

    auto *actMoveRight = viewMenu->addAction(QStringLiteral("分頁移到右邊面板"));
    actMoveRight->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Right));
    connect(actMoveRight, &QAction::triggered, this, [this] {
        m_area->moveActiveTabToPane(1);
        setCompareColumns(m_area->paneCount());
    });
    addAction(actMoveRight);

    auto *actMoveLeft = viewMenu->addAction(QStringLiteral("分頁移到左邊面板"));
    actMoveLeft->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Left));
    connect(actMoveLeft, &QAction::triggered, this, [this] {
        m_area->moveActiveTabToPane(-1);
        setCompareColumns(m_area->paneCount());
    });
    addAction(actMoveLeft);

    viewMenu->addSeparator();
    // QKeySequence::ZoomIn resolves to Ctrl++ on Linux, and '+' needs
    // Ctrl+Shift+= on most keyboards — so binding only that leaves the natural
    // Ctrl+= doing nothing. Bind Ctrl+= and the keypad +/- as well.
    //
    // 中：只綁 QKeySequence::ZoomIn 的話，按 Ctrl+= 不會有反應。
    auto *actZoomIn = viewMenu->addAction(QStringLiteral("放大"));
    actZoomIn->setShortcuts({ QKeySequence::ZoomIn,
                              QKeySequence(Qt::CTRL | Qt::Key_Equal),
                              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal),
                              QKeySequence(Qt::CTRL | Qt::Key_Plus) });
    connect(actZoomIn, &QAction::triggered, this, [this] { m_area->zoomIn(); });
    addAction(actZoomIn);

    auto *actZoomOut = viewMenu->addAction(QStringLiteral("縮小"));
    actZoomOut->setShortcuts({ QKeySequence::ZoomOut,
                               QKeySequence(Qt::CTRL | Qt::Key_Minus),
                               QKeySequence(Qt::CTRL | Qt::Key_Underscore) });
    connect(actZoomOut, &QAction::triggered, this, [this] { m_area->zoomOut(); });
    addAction(actZoomOut);

    auto *actZoomReset = viewMenu->addAction(QStringLiteral("原始大小"));
    actZoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(actZoomReset, &QAction::triggered, this, [this] { m_area->resetZoom(); });
    addAction(actZoomReset);

    viewMenu->addSeparator();
    auto *actFocusPath = viewMenu->addAction(QStringLiteral("聚焦路徑列"));
    actFocusPath->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(actFocusPath, &QAction::triggered, this, &MainWindow::focusPathBar);
    addAction(actFocusPath);

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("說明(&H)"));
    connect(helpMenu->addAction(QStringLiteral("關於")), &QAction::triggered, this, [this] {
        QMessageBox::information(
            this, QStringLiteral("關於 markdown-tool"),
            QStringLiteral("markdown-tool v" MD_VERSION "\n\n"
                           "md4c + QTextBrowser，無瀏覽器引擎。\n"
                           "mermaid 由外部 mmdc 渲染後快取為 PNG。\n\n"
                           "mermaid 渲染器: %1")
                .arg(m_cache->rendererAvailable() ? m_renderer->rendererId()
                                                  : QStringLiteral("未安裝")));
    });
}

bool MainWindow::openFile(const QString &path)
{
    DocumentView *view = m_area->openFile(path);
    if (!view)
        return false;

    const QFileInfo fi(view->path());
    if (m_sidebar->files()->root().isEmpty()
        || !view->path().startsWith(m_sidebar->files()->root()))
        m_sidebar->files()->setRoot(fi.absolutePath());
    m_sidebar->files()->selectFile(view->path());

    syncToActiveView();
    return true;
}

void MainWindow::syncToActiveView()
{
    DocumentView *view = activeView();

    if (!view) {
        setWindowTitle(QStringLiteral("markdown-tool"));
        m_pathBar->setPath(QString());
        m_sidebar->toc()->setToc({});
        updateStatus();
        return;
    }

    setWindowTitle(QStringLiteral("%1 — markdown-tool").arg(view->title()));
    m_pathBar->setPath(view->path());
    m_sidebar->toc()->setToc(view->document().toc);
    updateStatus();
}

void MainWindow::onOpenTriggered()
{
    DocumentView *view = activeView();
    const QString start = view && !view->path().isEmpty()
                              ? QFileInfo(view->path()).absolutePath()
                              : QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("開啟 markdown"), start,
        QStringLiteral("Markdown (*.md *.markdown *.mdx *.mdc *.mkd *.txt);;所有檔案 (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::onReloadTriggered()
{
    if (DocumentView *view = activeView(); view && !view->path().isEmpty())
        view->openFile(view->path());
}

void MainWindow::setMode(Theme::Mode mode)
{
    // What m_themeApplied is for: at startup the palette must be applied once
    // even when the mode has not changed. Otherwise the whole application keeps
    // the system (GTK) palette, which does not match the document colours —
    // observed as a slate-blue window that was neither theme.
    if (m_mode == mode && m_themeApplied)
        return;

    m_mode = mode;
    m_themeApplied = true;
    (mode == Theme::Dark ? m_actBlack : m_actWhite)->setChecked(true);
    m_area->setTheme(mode);

    // Apply to the whole application: the menu bar, tab labels and dialogs must
    // follow. Setting it only on MainWindow leaves QMenuBar and friends on the
    // default light palette.
    const QPalette pal = Theme::palette(m_mode);
    qApp->setPalette(pal);
    setPalette(pal);
    m_pathBar->setPalette(pal);
    m_sidebar->setPalette(pal);
}

void MainWindow::setCompareColumns(int columns)
{
    m_area->setPaneCount(columns);
    for (QAction *a : m_compareGroup->actions())
        if (a->data().toInt() == m_area->paneCount())
            a->setChecked(true);
    updateStatus();
}

void MainWindow::focusPathBar()
{
    m_pathBar->focusAndSelectAll();
}

void MainWindow::onPathSubmitted(const QString &path)
{
    const QFileInfo fi(path);

    if (fi.isDir()) {
        m_sidebar->files()->setRoot(fi.absoluteFilePath());
        m_sidebar->setCurrentIndex(1);          // Switch to the Files tab
        if (!m_actSidebar->isChecked())
            m_actSidebar->setChecked(true);
        updateStatus(QStringLiteral("已切換資料夾: ") + fi.absoluteFilePath());
        return;
    }

    if (!fi.exists()) {
        updateStatus(QStringLiteral("路徑不存在: ") + path);
        return;
    }

    if (!looksLikeMarkdown(path)) {
        updateStatus(QStringLiteral("不是 markdown 檔，交給系統開啟: ") + fi.fileName());
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
        return;
    }

    openFile(fi.absoluteFilePath());
}

void MainWindow::onLinkActivated(const QUrl &url)
{
    DocumentView *view = activeView();
    const QString basePath = view ? view->path() : QString();

    // A bare anchor
    if (!url.fragment().isEmpty() && url.path().isEmpty()) {
        if (view)
            view->scrollToAnchor(url.fragment());
        return;
    }
    const QString raw = url.toString();
    if (raw.startsWith(QLatin1Char('#'))) {
        if (view)
            view->scrollToAnchor(raw.mid(1));
        return;
    }

    // Link navigation replaces the content of the *same* tab rather than opening
    // a new one: an index page with dozens of links would otherwise spawn dozens
    // of tabs. New tabs come from the path bar, the file tree or drag and drop.
    //
    // 中：連結一律在原分頁換檔，否則索引頁點幾下就爆出一堆分頁。
    const auto navigateInPlace = [this, view](const QString &local) {
        if (!view) {
            openFile(local);
            return;
        }
        if (view->openFile(local)) {
            const QFileInfo fi(local);
            m_sidebar->files()->selectFile(fi.absoluteFilePath());
            syncToActiveView();
        }
    };

    // Relative path: resolve against the current document's directory
    if (url.isRelative() && !basePath.isEmpty()) {
        const QString local =
            QDir(QFileInfo(basePath).absolutePath()).absoluteFilePath(url.path());
        if (QFileInfo::exists(local)) {
            if (looksLikeMarkdown(local))
                navigateInPlace(local);
            else
                QDesktopServices::openUrl(QUrl::fromLocalFile(local));
            return;
        }
    }

    if (url.isLocalFile()) {
        const QString local = url.toLocalFile();
        if (looksLikeMarkdown(local) && QFileInfo::exists(local))
            navigateInPlace(local);
        else
            QDesktopServices::openUrl(url);
        return;
    }

    QDesktopServices::openUrl(url);
}

void MainWindow::updateStatus(const QString &transient)
{
    if (!transient.isEmpty())
        statusBar()->showMessage(transient, 8000);

    DocumentView *view = activeView();
    QStringList parts;

    if (m_area->count() > 1)
        parts << QStringLiteral("%1/%2 分頁").arg(m_area->activeIndex() + 1).arg(m_area->count());
    if (m_area->paneCount() > 1)
        parts << QStringLiteral("%1 格").arg(m_area->paneCount());

    if (view) {
        parts << QFileInfo(view->path()).fileName();
        parts << QStringLiteral("%1 段落").arg(view->document().toc.size());
        if (!view->document().mermaid.isEmpty())
            parts << QStringLiteral("mermaid %1 張").arg(view->document().mermaid.size());
    }

    // The queue is global (every tab shares one MermaidCache), so it cannot be
    // shown as a fraction of the current document's diagram count — three tabs
    // queued at once produced nonsense like "3/2".
    if (const int pending = m_cache->pendingCount(); pending > 0) {
        parts << QStringLiteral("產生中 %1 張").arg(pending);
    }
    m_statusRight->setText(parts.join(QStringLiteral("   |   ")));
}

void MainWindow::loadSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());

    // Black theme by default
    const bool dark = s.value(QStringLiteral("view/dark"), true).toBool();
    setMode(dark ? Theme::Dark : Theme::Light);

    const bool sidebarVisible = s.value(QStringLiteral("view/sidebar"), true).toBool();
    m_actSidebar->setChecked(sidebarVisible);
    m_sidebar->setVisible(sidebarVisible);

    const QVariant sizes = s.value(QStringLiteral("window/splitter"));
    if (sizes.isValid()) {
        QList<int> list;
        for (const QVariant &v : sizes.toList())
            list << v.toInt();
        if (list.size() == 2)
            m_splitter->setSizes(list);
    } else {
        m_splitter->setSizes({ 260, 940 });
    }

    m_sidebar->setCurrentIndex(s.value(QStringLiteral("view/sidebarTab"), 0).toInt());

    const QString lastRoot = s.value(QStringLiteral("files/root")).toString();
    if (!lastRoot.isEmpty() && QFileInfo(lastRoot).isDir())
        m_sidebar->files()->setRoot(lastRoot);

    setCompareColumns(s.value(QStringLiteral("view/panes"), 1).toInt());
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    s.setValue(QStringLiteral("view/dark"), m_mode == Theme::Dark);
    s.setValue(QStringLiteral("view/sidebar"), m_sidebar->isVisible());
    s.setValue(QStringLiteral("view/sidebarTab"), m_sidebar->currentIndex());
    s.setValue(QStringLiteral("view/panes"), m_area->paneCount());

    QVariantList sizes;
    for (int v : m_splitter->sizes())
        sizes << v;
    s.setValue(QStringLiteral("window/splitter"), sizes);
    s.setValue(QStringLiteral("files/root"), m_sidebar->files()->root());

    // Open tabs and the active index, restored on the next start
    s.setValue(QStringLiteral("files/openTabs"), m_area->openPaths());
    s.setValue(QStringLiteral("files/activeTab"), m_area->activeIndex());
    DocumentView *view = activeView();
    s.setValue(QStringLiteral("files/lastFile"), view ? view->path() : QString());
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    QMainWindow::closeEvent(e);
}
