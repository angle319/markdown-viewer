#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QImage>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QDropEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTreeView>
#include <QTreeWidget>

#include "DocumentView.h"
#include "MainWindow.h"
#include "PathBar.h"
#include "Theme.h"
#include "core/MmdcRenderer.h"

/// End-to-end: drives a real MainWindow through the flows a user actually
/// takes. Runs on the offscreen platform, so no X or Wayland is needed.
class TestE2eViewer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void opensFileAndTitleComesFromFirstH1();
    void tocPanelMirrorsHeadingStructure();
    void clickingTocEntryScrollsTheView();
    void scrollingUpdatesTocHighlight();
    void fileBrowserRootFollowsOpenedFileAndFiltersToMarkdown();
    void clickingFileInBrowserOpensIt();
    void relativeMarkdownLinkNavigates();
    void anchorLinkScrollsWithinDocument();
    void themeSwitchKeepsTocAndScroll();
    void themesAreWhiteAndBlack();
    void hardcodedInvisibleColourIsCorrected();

    // --- Path bar ---
    void pathBarShowsCurrentFile();
    void pathBarOpensTypedFile();
    void pathBarSwitchesFolderWhenGivenDirectory();
    void pathBarReportsMissingPathWithoutCrashing();
    void pathBarExpandsTildeAndRelativePaths();
    void ctrlLFocusesPathBar();

    // --- Drag and drop ---
    void dropOpensMarkdownFile();
    void dropFolderSwitchesFileBrowser();
    void dropIgnoresUnsupportedFile();
    void firstUsablePathPrefersMarkdownOverFolder();
    void zoomChangesFontSizeAndResets();
    void sidebarCanBeHidden();
    void externalEditTriggersReload();
    void missingImageDegradesToText();
    void mermaidPlaceholderIsReplacedByRealImage();

private:
    void writeFile(const QString &name, const QString &content) const;
    QString fixturePath(const QString &name) const;

    QTextBrowser *browser() const
    {
        // With tabs, m_win->findChild is wrong: it returns the first view ever
        // created, which is not necessarily the active one
        DocumentView *v = m_win->activeView();
        return v ? v->findChild<QTextBrowser *>() : nullptr;
    }
    QTreeWidget *tocTree() const { return m_win->findChild<QTreeWidget *>(); }
    QTabWidget *sidebar() const { return m_win->findChild<QTabWidget *>(); }
    QTreeView *fileTree() const;
    QAction *actionNamed(const QString &text) const;

    /// Display sizes of every image fragment in the document
    QList<QSize> imageSizes() const;
    int headingBlockCount() const;

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<MainWindow> m_win;
};

// ---------------------------------------------------------------- fixtures

static const char *kMainMd = R"(# 主文件標題

一段內文，其中有一個[相對連結](other.md)與一個[錨點](#第二節)。

## 第一節

內容一。

### 第一節之一

更深一層。

## 第二節

內容二。

```cpp
int main() { return 0; }
```

## 第三節

需要足夠長的內容才能捲動，以下是獨立段落（軟換行會被併段，撐不出高度）：

第 1 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 2 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 3 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 4 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 5 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 6 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 7 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 8 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 9 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 10 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 11 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 12 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 13 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 14 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 15 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 16 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 17 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 18 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 19 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 20 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 21 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 22 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 23 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 24 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 25 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 26 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 27 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 28 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 29 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 30 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 31 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 32 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 33 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 34 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 35 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 36 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 37 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 38 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 39 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 40 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 41 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 42 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 43 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 44 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 45 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 46 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 47 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 48 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 49 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 50 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 51 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 52 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 53 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 54 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 55 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 56 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 57 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 58 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 59 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。

第 60 段內容，這一段是為了把文件撐得比視窗高，好測捲動與 TOC 同步。
)";

static const char *kOtherMd = R"(# 另一份文件

這是 other.md。
)";

void TestE2eViewer::initTestCase()
{
    // Redirect QSettings and CacheLocation to a test-only location so the
    // user's own settings are never touched
    QStandardPaths::setTestModeEnabled(true);
}

void TestE2eViewer::init()
{
    // Every test starts from a cold cache: leftovers from a previous run would
    // skip the "placeholder first, real image later" path entirely and the test
    // would verify nothing.
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                              + QStringLiteral("/markdown-tool");
    if (QDir(cacheRoot).exists())
        QVERIFY(QDir(cacheRoot).removeRecursively());

    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    writeFile(QStringLiteral("main.md"), QString::fromUtf8(kMainMd));
    writeFile(QStringLiteral("other.md"), QString::fromUtf8(kOtherMd));
    writeFile(QStringLiteral("notes.txt"), QStringLiteral("純文字"));
    writeFile(QStringLiteral("ignore.cpp"), QStringLiteral("// 不該出現在檔案樹"));

    m_win.reset(new MainWindow);
    m_win->resize(1100, 700);
    m_win->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_win.data()));
}

void TestE2eViewer::cleanup()
{
    m_win.reset();
    m_dir.reset();
}

void TestE2eViewer::writeFile(const QString &name, const QString &content) const
{
    QFile f(m_dir->path() + QLatin1Char('/') + name);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write(content.toUtf8()) > 0);
}

QString TestE2eViewer::fixturePath(const QString &name) const
{
    return m_dir->path() + QLatin1Char('/') + name;
}

QTreeView *TestE2eViewer::fileTree() const
{
    // TocPanel uses a QTreeWidget (a QTreeView subclass) while the file tree is
    // a plain QTreeView, so pick the one whose model is a QFileSystemModel.
    for (QTreeView *v : m_win->findChildren<QTreeView *>())
        if (qobject_cast<QFileSystemModel *>(v->model()))
            return v;
    return nullptr;
}

QAction *TestE2eViewer::actionNamed(const QString &text) const
{
    for (QAction *a : m_win->findChildren<QAction *>())
        if (a->text() == text)
            return a;
    return nullptr;
}

QList<QSize> TestE2eViewer::imageSizes() const
{
    QList<QSize> out;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.isValid() && f.charFormat().isImageFormat()) {
                const QTextImageFormat fmt = f.charFormat().toImageFormat();
                out << QSize(int(fmt.width()), int(fmt.height()));
            }
        }
    }
    return out;
}

int TestE2eViewer::headingBlockCount() const
{
    int n = 0;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (b.blockFormat().headingLevel() > 0)
            ++n;
    return n;
}

// -------------------------------------------------------------------- tests

void TestE2eViewer::opensFileAndTitleComesFromFirstH1()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    QVERIFY2(m_win->windowTitle().startsWith(QStringLiteral("主文件標題")),
             qPrintable(m_win->windowTitle()));
    QVERIFY(m_win->windowTitle().contains(QStringLiteral("markdown-tool")));
    QVERIFY(!browser()->document()->isEmpty());
    QVERIFY(browser()->toPlainText().contains(QStringLiteral("內容一")));
}

void TestE2eViewer::tocPanelMirrorsHeadingStructure()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    QTreeWidget *tree = tocTree();
    QVERIFY(tree);

    // One top-level H1 with three H2 children, and an H3 under the first H2
    QCOMPARE(tree->topLevelItemCount(), 1);
    QTreeWidgetItem *h1 = tree->topLevelItem(0);
    QCOMPARE(h1->text(0), QStringLiteral("主文件標題"));
    QCOMPARE(h1->childCount(), 3);
    QCOMPARE(h1->child(0)->text(0), QStringLiteral("第一節"));
    QCOMPARE(h1->child(0)->childCount(), 1);
    QCOMPARE(h1->child(0)->child(0)->text(0), QStringLiteral("第一節之一"));
    QCOMPARE(h1->child(1)->text(0), QStringLiteral("第二節"));
    QCOMPARE(h1->child(2)->text(0), QStringLiteral("第三節"));

    // QTextDocument must also keep the headings as heading blocks, or scroll
    // synchronisation stops working
    QCOMPARE(headingBlockCount(), 5);
}

void TestE2eViewer::clickingTocEntryScrollsTheView()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    QVERIFY2(browser()->verticalScrollBar()->maximum() > 0, "文件不夠長，無法測捲動");
    QCOMPARE(browser()->verticalScrollBar()->value(), 0);

    QTreeWidget *tree = tocTree();
    QTreeWidgetItem *third = tree->topLevelItem(0)->child(2);   // 第三節
    const QRect rect = tree->visualItemRect(third);
    QVERIFY(rect.isValid());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());

    QTRY_VERIFY2(browser()->verticalScrollBar()->value() > 0,
                 "點擊 TOC 項目後畫面沒有捲動");
}

void TestE2eViewer::scrollingUpdatesTocHighlight()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    QTreeWidget *tree = tocTree();
    QVERIFY(tree);

    browser()->verticalScrollBar()->setValue(browser()->verticalScrollBar()->maximum());
    QTRY_VERIFY2(tree->currentItem() != nullptr, "捲到底部後 TOC 沒有高亮任何項目");

    // Scrolled to the bottom, the last heading should be current
    QCOMPARE(tree->currentItem()->text(0), QStringLiteral("第三節"));

    browser()->verticalScrollBar()->setValue(0);
    QTRY_COMPARE(tree->currentItem()->text(0), QStringLiteral("主文件標題"));
}

void TestE2eViewer::fileBrowserRootFollowsOpenedFileAndFiltersToMarkdown()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    QTreeView *files = fileTree();
    QVERIFY(files);
    auto *model = qobject_cast<QFileSystemModel *>(files->model());
    QVERIFY(model);

    const QModelIndex root = files->rootIndex();
    QVERIFY(root.isValid());
    QCOMPARE(model->filePath(root), QFileInfo(m_dir->path()).absoluteFilePath());

    // QFileSystemModel populates asynchronously
    QTRY_COMPARE(model->rowCount(root), 3);   // main.md / other.md / notes.txt

    QStringList names;
    for (int i = 0; i < model->rowCount(root); ++i)
        names << model->fileName(model->index(i, 0, root));
    names.sort();

    QCOMPARE(names, QStringList({ QStringLiteral("main.md"), QStringLiteral("notes.txt"),
                                  QStringLiteral("other.md") }));
    QVERIFY2(!names.contains(QStringLiteral("ignore.cpp")), "非 markdown 檔沒被過濾掉");
}

void TestE2eViewer::clickingFileInBrowserOpensIt()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    QTreeView *files = fileTree();
    auto *model = qobject_cast<QFileSystemModel *>(files->model());
    const QModelIndex root = files->rootIndex();
    QTRY_COMPARE(model->rowCount(root), 3);

    QModelIndex target;
    for (int i = 0; i < model->rowCount(root); ++i) {
        const QModelIndex idx = model->index(i, 0, root);
        if (model->fileName(idx) == QStringLiteral("other.md")) {
            target = idx;
            break;
        }
    }
    QVERIFY(target.isValid());

    files->scrollTo(target);
    const QRect r = files->visualRect(target);
    QVERIFY(r.isValid());
    QTest::mouseClick(files->viewport(), Qt::LeftButton, Qt::NoModifier, r.center());

    QTRY_VERIFY2(m_win->windowTitle().startsWith(QStringLiteral("另一份文件")),
                 qPrintable(m_win->windowTitle()));
}

void TestE2eViewer::relativeMarkdownLinkNavigates()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    // onLinkActivated is a private slot but still in the meta-object, so it can
    // be invoked by name
    QVERIFY(QMetaObject::invokeMethod(m_win.data(), "onLinkActivated",
                                      Q_ARG(QUrl, QUrl(QStringLiteral("other.md")))));

    QTRY_VERIFY2(m_win->windowTitle().startsWith(QStringLiteral("另一份文件")),
                 qPrintable(m_win->windowTitle()));
    QVERIFY(browser()->toPlainText().contains(QStringLiteral("這是 other.md")));
}

void TestE2eViewer::anchorLinkScrollsWithinDocument()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    QCOMPARE(browser()->verticalScrollBar()->value(), 0);

    QVERIFY(QMetaObject::invokeMethod(m_win.data(), "onLinkActivated",
                                      Q_ARG(QUrl, QUrl(QStringLiteral("#第三節")))));

    QTRY_VERIFY2(browser()->verticalScrollBar()->value() > 0, "錨點連結沒有捲動");
    // The title should stay on the same document; no new file was opened
    QVERIFY(m_win->windowTitle().startsWith(QStringLiteral("主文件標題")));
}

void TestE2eViewer::themeSwitchKeepsTocAndScroll()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    QAction *black = actionNamed(QStringLiteral("黑色主題"));
    QAction *white = actionNamed(QStringLiteral("白色主題"));
    QVERIFY(black && white);

    // Set the starting theme explicitly rather than assuming the default, which
    // is now black
    white->trigger();

    browser()->verticalScrollBar()->setValue(120);
    const int before = browser()->verticalScrollBar()->value();
    const QColor bgBefore = browser()->palette().color(QPalette::Base);

    black->trigger();
    QTRY_VERIFY(browser()->palette().color(QPalette::Base) != bgBefore);
    QCOMPARE(headingBlockCount(), 5);                     // TOC 結構沒掉
    QCOMPARE(tocTree()->topLevelItemCount(), 1);
    QVERIFY2(qAbs(browser()->verticalScrollBar()->value() - before) < 40,
             "切換主題後捲動位置跑掉太多");

    white->trigger();
    QTRY_COMPARE(browser()->palette().color(QPalette::Base), bgBefore);
}

void TestE2eViewer::themesAreWhiteAndBlack()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    actionNamed(QStringLiteral("白色主題"))->trigger();
    QTRY_COMPARE(browser()->palette().color(QPalette::Base), QColor(Qt::white));

    actionNamed(QStringLiteral("黑色主題"))->trigger();
    QTRY_COMPARE(browser()->palette().color(QPalette::Base), QColor(Qt::black));

    // The toggle action must work in both directions too
    QAction *toggle = actionNamed(QStringLiteral("切換主題"));
    QVERIFY(toggle);
    toggle->trigger();
    QTRY_COMPARE(browser()->palette().color(QPalette::Base), QColor(Qt::white));
    toggle->trigger();
    QTRY_COMPARE(browser()->palette().color(QPalette::Base), QColor(Qt::black));
}

void TestE2eViewer::hardcodedInvisibleColourIsCorrected()
{
    // Raw HTML hard-coding pure black. On the black theme it must be rewritten,
    // or the text is invisible
    writeFile(QStringLiteral("invisible.md"),
              QStringLiteral("# 對比\n\n<span style=\"color:#000000\">隱形候選</span>\n"));
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("invisible.md"))));
    actionNamed(QStringLiteral("黑色主題"))->trigger();

    bool found = false;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid() && !found; b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || !f.text().contains(QStringLiteral("隱形候選")))
                continue;
            found = true;
            const QColor fg = f.charFormat().foreground().style() != Qt::NoBrush
                                  ? f.charFormat().foreground().color()
                                  : QColor(Theme::colors(Theme::Dark).text);
            const double r = Theme::contrastRatio(fg, QColor(Qt::black));
            qInfo() << "修正後前景色 =" << fg.name() << " 對比 =" << r;
            QVERIFY2(r >= Theme::MinTextContrast,
                     qPrintable(QStringLiteral("寫死的 #000000 沒被修正，對比只有 %1:1").arg(r)));
        }
    }
    QVERIFY2(found, "找不到那段文字");
}

// ----------------------------------------------------------------- Path bar

void TestE2eViewer::pathBarShowsCurrentFile()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    PathBar *bar = m_win->findChild<PathBar *>();
    QVERIFY(bar);
    QCOMPARE(bar->path(), fixturePath(QStringLiteral("main.md")));
    QCOMPARE(bar->findChild<QLineEdit *>()->text(), fixturePath(QStringLiteral("main.md")));
}

void TestE2eViewer::pathBarOpensTypedFile()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    PathBar *bar = m_win->findChild<PathBar *>();
    QVERIFY(bar);

    QLineEdit *edit = bar->findChild<QLineEdit *>();
    QVERIFY(edit);
    edit->setText(fixturePath(QStringLiteral("other.md")));
    QTest::keyClick(edit, Qt::Key_Return);

    QTRY_VERIFY2(m_win->windowTitle().startsWith(QStringLiteral("另一份文件")),
                 qPrintable(m_win->windowTitle()));
    // The path bar must follow after opening
    QCOMPARE(bar->path(), fixturePath(QStringLiteral("other.md")));
}

void TestE2eViewer::pathBarSwitchesFolderWhenGivenDirectory()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    QVERIFY(QDir().mkpath(m_dir->path() + QStringLiteral("/sub")));
    writeFile(QStringLiteral("sub/inner.md"), QStringLiteral("# 子目錄文件\n"));

    PathBar *bar = m_win->findChild<PathBar *>();
    QLineEdit *edit = bar->findChild<QLineEdit *>();
    edit->setText(m_dir->path() + QStringLiteral("/sub"));
    QTest::keyClick(edit, Qt::Key_Return);

    // It should switch to the Files tab and re-root, not try to open the
    // directory as markdown
    QTRY_COMPARE(sidebar()->currentIndex(), 1);
    auto *model = qobject_cast<QFileSystemModel *>(fileTree()->model());
    QTRY_COMPARE(model->filePath(fileTree()->rootIndex()),
                 QFileInfo(m_dir->path() + QStringLiteral("/sub")).absoluteFilePath());
    // The current document stays open
    QVERIFY(m_win->windowTitle().startsWith(QStringLiteral("主文件標題")));
}

void TestE2eViewer::pathBarReportsMissingPathWithoutCrashing()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    const QString titleBefore = m_win->windowTitle();

    PathBar *bar = m_win->findChild<PathBar *>();
    QLineEdit *edit = bar->findChild<QLineEdit *>();
    edit->setText(m_dir->path() + QStringLiteral("/nope-does-not-exist.md"));
    QTest::keyClick(edit, Qt::Key_Return);

    QTRY_VERIFY(m_win->statusBar()->currentMessage().contains(QStringLiteral("路徑不存在")));
    QCOMPARE(m_win->windowTitle(), titleBefore);   // 沒換掉目前文件
}

void TestE2eViewer::pathBarExpandsTildeAndRelativePaths()
{
    // Pure logic; no file needs to be opened
    QCOMPARE(PathBar::resolveInput(QStringLiteral("~"), QString()), QDir::homePath());
    QCOMPARE(PathBar::resolveInput(QStringLiteral("~/x.md"), QString()),
             QDir::homePath() + QStringLiteral("/x.md"));
    QCOMPARE(PathBar::resolveInput(QStringLiteral("other.md"), QStringLiteral("/tmp/base")),
             QStringLiteral("/tmp/base/other.md"));
    QCOMPARE(PathBar::resolveInput(QStringLiteral("../up.md"), QStringLiteral("/tmp/base/sub")),
             QStringLiteral("/tmp/base/up.md"));
    QCOMPARE(PathBar::resolveInput(QStringLiteral("  /abs/path.md  "), QStringLiteral("/tmp")),
             QStringLiteral("/abs/path.md"));
    QCOMPARE(PathBar::resolveInput(QStringLiteral("file:///tmp/x.md"), QString()),
             QStringLiteral("/tmp/x.md"));
    QVERIFY(PathBar::resolveInput(QStringLiteral("   "), QStringLiteral("/tmp")).isEmpty());
}

void TestE2eViewer::ctrlLFocusesPathBar()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    PathBar *bar = m_win->findChild<PathBar *>();
    QLineEdit *edit = bar->findChild<QLineEdit *>();
    QVERIFY(!edit->hasFocus());

    QAction *focus = actionNamed(QStringLiteral("聚焦路徑列"));
    QVERIFY(focus);
    QCOMPARE(focus->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_L));
    focus->trigger();

    QTRY_VERIFY2(edit->hasFocus(), "Ctrl+L 沒把焦點給路徑列");
    QVERIFY2(!edit->selectedText().isEmpty(), "聚焦後沒有全選，覆寫路徑會很麻煩");

    // Escape restores the path and hands focus back to the document
    edit->setText(QStringLiteral("/tmp/whatever.md"));
    QTest::keyClick(edit, Qt::Key_Escape);
    QCOMPARE(edit->text(), fixturePath(QStringLiteral("main.md")));
}

// ----------------------------------------------------------- Drag and drop

/// Simulates dropping a set of paths.
///
/// Goes through openFromUrls() rather than synthesising a QDropEvent:
/// QWidget::event() is protected and QApplication::notify routes drag and drop
/// through QDragManager, so a synthetic event never reaches dropEvent in a test.
/// dropEvent is itself a thin adapter over openFromUrls; that the window accepts
/// drops and child widgets do not intercept them is asserted separately.
static void sendDrop(MainWindow *win, const QStringList &paths)
{
    QList<QUrl> urls;
    for (const QString &p : paths)
        urls << QUrl::fromLocalFile(p);
    win->openFromUrls(urls);
}

void TestE2eViewer::dropOpensMarkdownFile()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    QVERIFY2(m_win->acceptDrops(), "視窗沒有開啟拖放");

    // A child that accepts the drop stops it bubbling up to MainWindow
    QVERIFY2(!browser()->acceptDrops(), "QTextBrowser 會攔截 drop");
    QVERIFY2(!sidebar()->acceptDrops(), "側邊欄會攔截 drop");
    for (QWidget *w : sidebar()->findChildren<QWidget *>())
        QVERIFY2(!w->acceptDrops(),
                 qPrintable(QStringLiteral("%1 會攔截 drop")
                                .arg(QString::fromLatin1(w->metaObject()->className()))));

    sendDrop(m_win.data(), { fixturePath(QStringLiteral("other.md")) });

    QTRY_VERIFY2(m_win->windowTitle().startsWith(QStringLiteral("另一份文件")),
                 qPrintable(m_win->windowTitle()));
    // The path bar must follow
    QCOMPARE(m_win->findChild<PathBar *>()->path(),
             fixturePath(QStringLiteral("other.md")));
}

void TestE2eViewer::dropFolderSwitchesFileBrowser()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    QVERIFY(QDir().mkpath(m_dir->path() + QStringLiteral("/dropped")));
    writeFile(QStringLiteral("dropped/inner.md"), QStringLiteral("# 拖入的資料夾\n"));

    sendDrop(m_win.data(), { m_dir->path() + QStringLiteral("/dropped") });

    QTRY_COMPARE(sidebar()->currentIndex(), 1);
    auto *model = qobject_cast<QFileSystemModel *>(fileTree()->model());
    QTRY_COMPARE(model->filePath(fileTree()->rootIndex()),
                 QFileInfo(m_dir->path() + QStringLiteral("/dropped")).absoluteFilePath());
    // A directory must not replace the current document as if it were markdown
    QVERIFY(m_win->windowTitle().startsWith(QStringLiteral("主文件標題")));
}

void TestE2eViewer::dropIgnoresUnsupportedFile()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    const QString before = m_win->windowTitle();

    sendDrop(m_win.data(), { fixturePath(QStringLiteral("ignore.cpp")) });

    QCOMPARE(m_win->windowTitle(), before);   // 沒換掉目前文件
    QTRY_VERIFY(m_win->statusBar()->currentMessage().contains(QStringLiteral("不是 markdown")));
}

void TestE2eViewer::firstUsablePathPrefersMarkdownOverFolder()
{
    const QString md = fixturePath(QStringLiteral("main.md"));
    const QString cpp = fixturePath(QStringLiteral("ignore.cpp"));
    QVERIFY(QDir().mkpath(m_dir->path() + QStringLiteral("/adir")));
    const QString dir = m_dir->path() + QStringLiteral("/adir");

    // With several items dropped, opening a file beats re-rooting
    QCOMPARE(MainWindow::firstUsablePath({ QUrl::fromLocalFile(dir),
                                           QUrl::fromLocalFile(md) }),
             QFileInfo(md).absoluteFilePath());
    QCOMPARE(MainWindow::firstUsablePath({ QUrl::fromLocalFile(dir) }),
             QFileInfo(dir).absoluteFilePath());
    QVERIFY(MainWindow::firstUsablePath({ QUrl::fromLocalFile(cpp) }).isEmpty());
    QVERIFY(MainWindow::firstUsablePath({ QUrl(QStringLiteral("https://example.com/a.md")) })
                .isEmpty());
    QVERIFY(MainWindow::firstUsablePath({}).isEmpty());
}

void TestE2eViewer::zoomChangesFontSizeAndResets()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    // Zoom is driven by the document's default font now, not QTextEdit::zoomIn
    // on the widget font, because that leaves explicitly sized headings behind
    const qreal base = browser()->document()->defaultFont().pointSizeF();
    QVERIFY(base > 0);

    QAction *zin = actionNamed(QStringLiteral("放大"));
    QAction *zout = actionNamed(QStringLiteral("縮小"));
    QAction *zreset = actionNamed(QStringLiteral("原始大小"));
    QVERIFY(zin && zout && zreset);

    const auto size = [this] { return browser()->document()->defaultFont().pointSizeF(); };

    zin->trigger();
    zin->trigger();
    QVERIFY2(size() > base, "放大沒有生效");

    zreset->trigger();
    QCOMPARE(size(), base);

    zout->trigger();
    QVERIFY2(size() < base, "縮小沒有生效");
    zreset->trigger();
    QCOMPARE(size(), base);
}

void TestE2eViewer::sidebarCanBeHidden()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));

    QTabWidget *side = sidebar();
    QVERIFY(side);
    QCOMPARE(side->count(), 2);
    QCOMPARE(side->tabText(0), QStringLiteral("段落"));
    QCOMPARE(side->tabText(1), QStringLiteral("檔案"));
    QVERIFY(side->isVisible());

    QAction *act = actionNamed(QStringLiteral("顯示側邊欄"));
    QVERIFY(act);
    act->setChecked(false);
    QTRY_VERIFY(!side->isVisible());
    act->setChecked(true);
    QTRY_VERIFY(side->isVisible());
}

void TestE2eViewer::externalEditTriggersReload()
{
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("main.md"))));
    QCOMPARE(tocTree()->topLevelItem(0)->childCount(), 3);

    browser()->verticalScrollBar()->setValue(80);
    const int before = browser()->verticalScrollBar()->value();

    // External edit: append an H2
    QFile f(fixturePath(QStringLiteral("main.md")));
    QVERIFY(f.open(QIODevice::Append));
    f.write(QString::fromUtf8("\n## 新增的第四節\n\n新內容。\n").toUtf8());
    f.close();

    QTRY_VERIFY_WITH_TIMEOUT(tocTree()->topLevelItem(0)->childCount() == 4, 8000);
    QCOMPARE(tocTree()->topLevelItem(0)->child(3)->text(0), QStringLiteral("新增的第四節"));
    QVERIFY2(qAbs(browser()->verticalScrollBar()->value() - before) < 40,
             "自動重載後捲動位置沒有保留");
}

void TestE2eViewer::missingImageDegradesToText()
{
    writeFile(QStringLiteral("img.md"),
              QStringLiteral("# 圖片測試\n\n![替代文字](nope.png)\n"));
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("img.md"))));

    const QString text = browser()->toPlainText();
    QVERIFY2(text.contains(QStringLiteral("[缺少圖片: nope.png]")), qPrintable(text));
}

void TestE2eViewer::mermaidPlaceholderIsReplacedByRealImage()
{
    if (!MmdcRenderer().isAvailable())
        QSKIP("找不到 mmdc，跳過 mermaid e2e");

    writeFile(QStringLiteral("mm.md"),
              QStringLiteral("# 圖表\n\n```mermaid\nflowchart LR\n  E2E[端到端] --> OK[通過]\n```\n"));
    QVERIFY(m_win->openFile(fixturePath(QStringLiteral("mm.md"))));

    // It starts as the "rendering" placeholder, which has a fixed height of 88
    QTRY_VERIFY(!imageSizes().isEmpty());
    QCOMPARE(imageSizes().size(), 1);
    QCOMPARE(imageSizes().first().height(), 88);

    // Once mmdc finishes it must be swapped for the real image, no longer 88
    QTRY_VERIFY_WITH_TIMEOUT(!imageSizes().isEmpty()
                                 && imageSizes().first().height() != 88, 90000);

    const QSize real = imageSizes().first();
    qInfo() << "mermaid 實際顯示尺寸 =" << real;
    QVERIFY2(real.width() > 60 && real.height() > 20,
             qPrintable(QStringLiteral("圖太小: %1x%2").arg(real.width()).arg(real.height())));
    QVERIFY2(real.width() <= 980, "圖沒有被限制在內容欄寬內");
}

QTEST_MAIN(TestE2eViewer)
#include "test_e2e_viewer.moc"
