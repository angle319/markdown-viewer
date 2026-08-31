#include "MainWindow.h"

#include "Sidebar.h"
#include "FileBrowserPanel.h"
#include "TocPanel.h"
#include "core/FileWatcher.h"
#include "core/MarkdownParser.h"
#include "core/MermaidCache.h"
#include "core/MmdcRenderer.h"
#include "render/TextBrowserBackend.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTextStream>

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
    m_cache = new MermaidCache(m_renderer, this);
    m_watcher = new FileWatcher(this);

    buildUi();
    buildMenus();
    loadSettings();
    updateStatus();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    m_sidebar = new Sidebar(this);
    auto *backend = new TextBrowserBackend(m_cache, this);
    m_backend = backend;

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_backend->widget());
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);

    m_statusRight = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusRight);

    connect(m_sidebar->toc(), &TocPanel::anchorActivated,
            this, [this](const QString &a) { m_backend->scrollToAnchor(a); });
    connect(m_backend, &IRenderBackend::currentTocIndexChanged,
            m_sidebar->toc(), &TocPanel::highlightIndex);
    connect(m_backend, &IRenderBackend::linkActivated,
            this, &MainWindow::onLinkActivated);
    connect(m_sidebar->files(), &FileBrowserPanel::fileActivated,
            this, [this](const QString &p) { openFile(p); });

    connect(m_watcher, &FileWatcher::fileChanged, this, [this] {
        QFile f(m_path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        m_markdown = in.readAll();
        reparse(true);
        updateStatus(QStringLiteral("已重新載入"));
    });

    connect(m_cache, &MermaidCache::rendered, this, [this](const QString &key, const QString &) {
        m_backend->mermaidReady(key);
        updateStatus();
    });
    connect(m_cache, &MermaidCache::failed, this, [this](const QString &, const QString &err) {
        updateStatus(QStringLiteral("mermaid 渲染失敗: ") + err.left(160));
    });
    connect(m_cache, &MermaidCache::idle, this, [this] { updateStatus(); });

    resize(1200, 820);
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

    m_actTheme = viewMenu->addAction(QStringLiteral("暗色主題"));
    m_actTheme->setCheckable(true);
    m_actTheme->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_T));
    connect(m_actTheme, &QAction::toggled, this, &MainWindow::onThemeToggled);

    viewMenu->addSeparator();
    auto *actZoomIn = viewMenu->addAction(QStringLiteral("放大"));
    actZoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(actZoomIn, &QAction::triggered, this, [this] { m_backend->zoomIn(); });

    auto *actZoomOut = viewMenu->addAction(QStringLiteral("縮小"));
    actZoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(actZoomOut, &QAction::triggered, this, [this] { m_backend->zoomOut(); });

    auto *actZoomReset = viewMenu->addAction(QStringLiteral("原始大小"));
    actZoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(actZoomReset, &QAction::triggered, this, [this] { m_backend->resetZoom(); });

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("說明(&H)"));
    connect(helpMenu->addAction(QStringLiteral("關於")), &QAction::triggered, this, [this] {
        QMessageBox::information(
            this, QStringLiteral("關於 markdown-tool"),
            QStringLiteral("markdown-tool v0.1\n\n"
                           "md4c + QTextBrowser，無瀏覽器引擎。\n"
                           "mermaid 由外部 mmdc 渲染後快取為 SVG。\n\n"
                           "mermaid 渲染器: %1")
                .arg(m_cache->rendererAvailable() ? m_renderer->rendererId()
                                                  : QStringLiteral("未安裝")));
    });
}

bool MainWindow::openFile(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.isFile()) {
        updateStatus(QStringLiteral("找不到檔案: ") + path);
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

    if (m_sidebar->files()->root().isEmpty()
        || !m_path.startsWith(m_sidebar->files()->root()))
        m_sidebar->files()->setRoot(fi.absolutePath());
    m_sidebar->files()->selectFile(m_path);

    updateStatus();
    return true;
}

void MainWindow::reparse(bool preserveScroll)
{
    MarkdownParser::Options opt;
    // mmdc 不在就別產生佔位圖，直接把 mermaid 當程式碼區塊顯示
    opt.mermaidEnabled = m_cache->rendererAvailable();
    opt.darkTheme = (m_mode == Theme::Dark);

    const int scroll = preserveScroll ? m_backend->scrollValue() : 0;

    m_doc = MarkdownParser::parse(m_markdown, QFileInfo(m_path).absolutePath(), opt);
    m_backend->setTheme(m_mode);
    m_backend->setDocument(m_doc);
    m_sidebar->toc()->setToc(m_doc.toc);

    if (preserveScroll)
        m_backend->setScrollValue(scroll);

    const QString title = m_doc.title.isEmpty() ? QFileInfo(m_path).fileName() : m_doc.title;
    setWindowTitle(m_path.isEmpty() ? QStringLiteral("markdown-tool")
                                    : QStringLiteral("%1 — markdown-tool").arg(title));

    if (!m_doc.mermaid.isEmpty() && !m_cache->rendererAvailable() && !m_degradeNoticeShown) {
        m_degradeNoticeShown = true;
        updateStatus(QStringLiteral("未找到 mmdc，mermaid 以原始碼顯示。"
                                    "安裝: npm i -g @mermaid-js/mermaid-cli"));
    }
}

void MainWindow::onOpenTriggered()
{
    const QString start = m_path.isEmpty() ? QDir::homePath() : QFileInfo(m_path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("開啟 markdown"), start,
        QStringLiteral("Markdown (*.md *.markdown *.mdx *.mdc *.mkd *.txt);;所有檔案 (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::onReloadTriggered()
{
    if (m_path.isEmpty())
        return;
    openFile(m_path);
}

void MainWindow::onThemeToggled(bool dark)
{
    m_mode = dark ? Theme::Dark : Theme::Light;
    // 語法高亮的配色是在產 HTML 時決定的，所以主題切換必須重新解析
    if (!m_markdown.isEmpty())
        reparse(true);
    else
        m_backend->setTheme(m_mode);
}

void MainWindow::onLinkActivated(const QUrl &url)
{
    // 純錨點
    if (!url.fragment().isEmpty() && url.path().isEmpty()) {
        m_backend->scrollToAnchor(url.fragment());
        return;
    }
    const QString raw = url.toString();
    if (raw.startsWith(QLatin1Char('#'))) {
        m_backend->scrollToAnchor(raw.mid(1));
        return;
    }

    // 相對路徑：對目前檔案所在目錄解析
    if (url.isRelative() && !m_path.isEmpty()) {
        const QString local =
            QDir(QFileInfo(m_path).absolutePath()).absoluteFilePath(url.path());
        if (QFileInfo::exists(local)) {
            if (looksLikeMarkdown(local))
                openFile(local);
            else
                QDesktopServices::openUrl(QUrl::fromLocalFile(local));
            return;
        }
    }

    if (url.isLocalFile()) {
        const QString local = url.toLocalFile();
        if (looksLikeMarkdown(local) && QFileInfo::exists(local))
            openFile(local);
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

    QStringList parts;
    if (!m_path.isEmpty())
        parts << QFileInfo(m_path).fileName();
    parts << QStringLiteral("%1 段落").arg(m_doc.toc.size());
    if (!m_doc.mermaid.isEmpty()) {
        const int pending = m_cache->pendingCount();
        parts << (pending > 0 ? QStringLiteral("mermaid %1/%2 產生中")
                                    .arg(pending).arg(m_doc.mermaid.size())
                              : QStringLiteral("mermaid %1 張").arg(m_doc.mermaid.size()));
    }
    m_statusRight->setText(parts.join(QStringLiteral("   |   ")));
}

void MainWindow::loadSettings()
{
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());

    const bool dark = s.value(QStringLiteral("view/dark"), false).toBool();
    m_actTheme->setChecked(dark);   // 觸發 onThemeToggled

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
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    s.setValue(QStringLiteral("view/dark"), m_mode == Theme::Dark);
    s.setValue(QStringLiteral("view/sidebar"), m_sidebar->isVisible());
    s.setValue(QStringLiteral("view/sidebarTab"), m_sidebar->currentIndex());

    QVariantList sizes;
    for (int v : m_splitter->sizes())
        sizes << v;
    s.setValue(QStringLiteral("window/splitter"), sizes);
    s.setValue(QStringLiteral("files/root"), m_sidebar->files()->root());
    s.setValue(QStringLiteral("files/lastFile"), m_path);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    QMainWindow::closeEvent(e);
}
