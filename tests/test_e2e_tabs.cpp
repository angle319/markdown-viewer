#include <QtTest>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeWidget>

#include "DocumentArea.h"
#include "PaneGroup.h"
#include "DocumentView.h"
#include "MainWindow.h"
#include "PathBar.h"
#include "Theme.h"

/// 多文件：分頁與比較模式。
class TestE2eTabs : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // --- 分頁 ---
    void openingTwoFilesCreatesTwoTabs();
    void reopeningSameFileSwitchesInsteadOfDuplicating();
    void tabTitleComesFromDocumentHeading();
    void switchingTabUpdatesPathBarAndToc();
    void closingTabRemovesItAndKeepsOthers();
    void closingLastTabShowsEmptyState();
    void tabsCanBeReordered();
    void eachTabWatchesItsOwnFile();
    void linkNavigationStaysInSameTab();

    // --- 分割面板（VS Code 式的 editor group）---
    void splittingGivesEachPaneItsOwnTabBar();
    void paneCountNeverExceedsDocumentCount();
    void mergingPanesBringsDocumentsBack();
    void movingTabToAdjacentPaneCreatesOne();
    void dropZoneIsComputedFromPosition();
    void droppingTabOnEdgeSplits();
    void droppingTabIntoAnotherPaneMovesIt();
    void emptyPaneIsRemovedAutomatically();
    void activePaneIsMarkedOnlyWhenSplit();

    // --- 全域操作套用到所有分頁 ---
    void themeAppliesToEveryTab();
    void zoomAppliesToEveryTab();

    // --- 可選的視覺輸出 ---
    void dumpScreenshotsIfRequested();

private:
    QString make(const QString &name, const QString &title, int paragraphs = 3) const;
    QTabBar *tabBar() const { return m_win->findChild<QTabBar *>(); }
    DocumentArea *area() const { return m_win->area(); }
    int visibleCount() const { return area()->visibleViews().size(); }

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<MainWindow> m_win;
};

void TestE2eTabs::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestE2eTabs::init()
{
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                              + QStringLiteral("/markdown-tool");
    if (QDir(cacheRoot).exists())
        QVERIFY(QDir(cacheRoot).removeRecursively());

    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    m_win.reset(new MainWindow);
    m_win->resize(1400, 800);
    m_win->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_win.data()));
}

void TestE2eTabs::cleanup()
{
    m_win.reset();
    m_dir.reset();
}

QString TestE2eTabs::make(const QString &name, const QString &title, int paragraphs) const
{
    QString md = QStringLiteral("# %1\n\n").arg(title);
    for (int i = 1; i <= paragraphs; ++i)
        md += QStringLiteral("## %1 的第 %2 節\n\n內容 %2。\n\n").arg(title).arg(i);

    const QString path = m_dir->path() + QLatin1Char('/') + name;
    QFile f(path);
    [&] { QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); }();
    f.write(md.toUtf8());
    f.close();
    return path;
}

// ------------------------------------------------------------------ 分頁

void TestE2eTabs::openingTwoFilesCreatesTwoTabs()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QCOMPARE(area()->count(), 1);

    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));
    QCOMPARE(area()->count(), 2);
    QCOMPARE(area()->activeIndex(), 1);
    QCOMPARE(tabBar()->count(), 2);

    // 兩個分頁各自持有不同的文件
    QVERIFY(area()->viewAt(0) != area()->viewAt(1));
    QCOMPARE(area()->viewAt(0)->title(), QStringLiteral("甲"));
    QCOMPARE(area()->viewAt(1)->title(), QStringLiteral("乙"));
}

void TestE2eTabs::reopeningSameFileSwitchesInsteadOfDuplicating()
{
    const QString a = make(QStringLiteral("a.md"), QStringLiteral("甲"));
    const QString b = make(QStringLiteral("b.md"), QStringLiteral("乙"));
    QVERIFY(m_win->openFile(a));
    QVERIFY(m_win->openFile(b));
    QCOMPARE(area()->count(), 2);
    QCOMPARE(area()->activeIndex(), 1);

    QVERIFY(m_win->openFile(a));
    QCOMPARE(area()->count(), 2);          // 沒有多開
    QCOMPARE(area()->activeIndex(), 0);    // 切到既有那個

    // 相對路徑也要認得出是同一個檔
    QVERIFY(m_win->openFile(m_dir->path() + QStringLiteral("/./a.md")));
    QCOMPARE(area()->count(), 2);
}

void TestE2eTabs::tabTitleComesFromDocumentHeading()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("我的標題"))));
    QCOMPARE(tabBar()->tabText(0), QStringLiteral("我的標題"));
    QCOMPARE(tabBar()->tabToolTip(0), area()->viewAt(0)->path());

    // 沒有 H1 時用檔名
    const QString path = m_dir->path() + QStringLiteral("/noheading.md");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QStringLiteral("只有內文，沒有標題。\n").toUtf8());
    f.close();

    QVERIFY(m_win->openFile(path));
    QCOMPARE(tabBar()->tabText(1), QStringLiteral("noheading.md"));
}

void TestE2eTabs::switchingTabUpdatesPathBarAndToc()
{
    const QString a = make(QStringLiteral("a.md"), QStringLiteral("甲"), 2);
    const QString b = make(QStringLiteral("b.md"), QStringLiteral("乙"), 5);
    QVERIFY(m_win->openFile(a));
    QVERIFY(m_win->openFile(b));

    auto *bar = m_win->findChild<PathBar *>();
    auto *toc = m_win->findChild<QTreeWidget *>();
    QVERIFY(bar && toc);

    QCOMPARE(bar->path(), b);
    QCOMPARE(m_win->windowTitle().left(1), QStringLiteral("乙"));
    QCOMPARE(toc->topLevelItem(0)->childCount(), 5);   // 乙 有 5 個 H2

    area()->setActiveIndex(0);
    QCOMPARE(bar->path(), a);
    QCOMPARE(m_win->windowTitle().left(1), QStringLiteral("甲"));
    QTRY_COMPARE(toc->topLevelItem(0)->childCount(), 2);
}

void TestE2eTabs::closingTabRemovesItAndKeepsOthers()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("c.md"), QStringLiteral("丙"))));
    QCOMPARE(area()->count(), 3);

    area()->closeTab(1);                   // 關掉「乙」
    QCOMPARE(area()->count(), 2);

    QStringList titles;
    for (int i = 0; i < area()->count(); ++i)
        titles << area()->viewAt(i)->title();
    QCOMPARE(titles, QStringList({ QStringLiteral("甲"), QStringLiteral("丙") }));
    QVERIFY(area()->activeView() != nullptr);
}

void TestE2eTabs::closingLastTabShowsEmptyState()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    area()->closeActiveTab();

    QCOMPARE(area()->count(), 0);
    QVERIFY(area()->activeView() == nullptr);
    QCOMPARE(m_win->windowTitle(), QStringLiteral("markdown-tool"));
    QCOMPARE(m_win->findChild<PathBar *>()->path(), QString());
    QCOMPARE(m_win->findChild<QTreeWidget *>()->topLevelItemCount(), 0);

    // 空狀態提示要看得到
    bool sawPlaceholder = false;
    for (QLabel *l : area()->findChildren<QLabel *>())
        if (l->isVisible() && l->text().contains(QStringLiteral("沒有開啟任何檔案")))
            sawPlaceholder = true;
    QVERIFY2(sawPlaceholder, "關掉最後一個分頁後沒有空狀態提示");

    // 還能再開
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));
    QCOMPARE(area()->count(), 1);
}

void TestE2eTabs::tabsCanBeReordered()
{
    const QString a = make(QStringLiteral("a.md"), QStringLiteral("甲"));
    const QString b = make(QStringLiteral("b.md"), QStringLiteral("乙"));
    QVERIFY(m_win->openFile(a));
    QVERIFY(m_win->openFile(b));
    QCOMPARE(area()->openPaths(), QStringList({ a, b }));

    tabBar()->moveTab(0, 1);               // 使用者拖曳排序
    QCOMPARE(area()->openPaths(), QStringList({ b, a }));
    QCOMPARE(area()->viewAt(0)->title(), QStringLiteral("乙"));
}

void TestE2eTabs::eachTabWatchesItsOwnFile()
{
    const QString a = make(QStringLiteral("a.md"), QStringLiteral("甲"), 2);
    const QString b = make(QStringLiteral("b.md"), QStringLiteral("乙"), 2);
    QVERIFY(m_win->openFile(a));
    QVERIFY(m_win->openFile(b));
    area()->setActiveIndex(1);             // 作用中是「乙」

    // 改「甲」的檔案：即使它不是作用中的分頁，也該重新載入
    QFile f(a);
    QVERIFY(f.open(QIODevice::Append));
    f.write(QStringLiteral("\n## 甲 的第 3 節\n\n新內容。\n").toUtf8());
    f.close();

    QTRY_VERIFY_WITH_TIMEOUT(area()->viewAt(0)->document().toc.size() == 4, 8000);
    // 作用中的分頁不受影響
    QCOMPARE(area()->activeIndex(), 1);
    QCOMPARE(area()->viewAt(1)->document().toc.size(), 3);
}

void TestE2eTabs::linkNavigationStaysInSameTab()
{
    const QString target = make(QStringLiteral("target.md"), QStringLiteral("目標"));
    const QString src = m_dir->path() + QStringLiteral("/src.md");
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QStringLiteral("# 來源\n\n[去目標](target.md)\n").toUtf8());
    f.close();

    QVERIFY(m_win->openFile(src));
    QCOMPARE(area()->count(), 1);

    QVERIFY(QMetaObject::invokeMethod(m_win.data(), "onLinkActivated",
                                      Q_ARG(QUrl, QUrl(QStringLiteral("target.md")))));

    // 關鍵：在同一個分頁內換檔，不新增分頁。
    // 像 INDEX.md 那種幾十個連結的索引頁，每點一次開一個分頁很快就爆掉。
    QTRY_COMPARE(area()->count(), 1);
    QCOMPARE(area()->activeView()->title(), QStringLiteral("目標"));
    QCOMPARE(tabBar()->tabText(0), QStringLiteral("目標"));
    QCOMPARE(m_win->findChild<PathBar *>()->path(), QFileInfo(target).absoluteFilePath());
}

// -------------------------------------------------------------- 比較模式

void TestE2eTabs::splittingGivesEachPaneItsOwnTabBar()
{
    for (const QString &n : { QStringLiteral("a.md"), QStringLiteral("b.md"),
                              QStringLiteral("c.md") })
        QVERIFY(m_win->openFile(make(n, n.left(1).toUpper())));
    QCOMPARE(area()->count(), 3);
    QCOMPARE(area()->paneCount(), 1);

    area()->setPaneCount(2);
    QCOMPARE(area()->paneCount(), 2);

    // 關鍵：每一格都有自己的分頁列，而且列出的正是屬於它的文件。
    // 先前的單一全域分頁列做不到這件事 —— 看不出哪個分頁對應哪一格。
    int totalTabs = 0;
    for (int i = 0; i < area()->paneCount(); ++i) {
        PaneGroup *pane = area()->paneAt(i);
        QVERIFY(pane);
        QVERIFY2(pane->tabBar() != nullptr, "面板沒有自己的分頁列");
        QVERIFY2(pane->count() > 0, "面板是空的");
        QCOMPARE(pane->tabBar()->count(), pane->count());
        for (int t = 0; t < pane->count(); ++t)
            QCOMPARE(pane->tabBar()->tabText(t), pane->viewAt(t)->title());
        totalTabs += pane->count();
    }
    QCOMPARE(totalTabs, 3);              // 文件沒有遺失，只是換了格子
    QCOMPARE(area()->visibleViews().size(), 2);
}

void TestE2eTabs::paneCountNeverExceedsDocumentCount()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));

    area()->setPaneCount(4);             // 只有 2 份文件
    QCOMPARE(area()->paneCount(), 2);    // 不做出空面板

    area()->setPaneCount(99);
    QVERIFY(area()->paneCount() <= DocumentArea::MaxPanes);
}

void TestE2eTabs::mergingPanesBringsDocumentsBack()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));
    area()->setPaneCount(2);
    QCOMPARE(area()->paneCount(), 2);

    area()->setPaneCount(1);
    QCOMPARE(area()->paneCount(), 1);
    QCOMPARE(area()->count(), 2);        // 兩份文件都還在，併回同一格
    QCOMPARE(area()->paneAt(0)->count(), 2);
}

void TestE2eTabs::movingTabToAdjacentPaneCreatesOne()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));
    QCOMPARE(area()->paneCount(), 1);

    area()->moveActiveTabToPane(1);      // 沒有右邊的面板 → 新建一個
    QCOMPARE(area()->paneCount(), 2);
    QCOMPARE(area()->paneAt(0)->count(), 1);
    QCOMPARE(area()->paneAt(1)->count(), 1);
    QCOMPARE(area()->paneAt(1)->currentView()->title(), QStringLiteral("乙"));
    QCOMPARE(area()->activeGroup(), area()->paneAt(1));
}

void TestE2eTabs::dropZoneIsComputedFromPosition()
{
    const QRect pane(0, 0, 400, 300);
    QCOMPARE(PaneGroup::zoneFor(pane, QPoint(10, 150)), PaneGroup::DropZone::SplitLeft);
    QCOMPARE(PaneGroup::zoneFor(pane, QPoint(390, 150)), PaneGroup::DropZone::SplitRight);
    QCOMPARE(PaneGroup::zoneFor(pane, QPoint(200, 150)), PaneGroup::DropZone::Into);

    // 很窄的面板也要留得住可用的邊緣區
    const QRect narrow(0, 0, 100, 300);
    QCOMPARE(PaneGroup::zoneFor(narrow, QPoint(5, 10)), PaneGroup::DropZone::SplitLeft);
    QCOMPARE(PaneGroup::zoneFor(narrow, QPoint(95, 10)), PaneGroup::DropZone::SplitRight);
}

void TestE2eTabs::droppingTabOnEdgeSplits()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));
    PaneGroup *pane = area()->paneAt(0);
    QCOMPARE(area()->paneCount(), 1);

    // 把「甲」拖到同一格的右緣 → 在右邊開一格
    // （真正的 QDrag 序列在測試裡跑不起來，所以驗的是 dropEvent 轉接的那個方法）
    area()->moveTabToPane(pane, pane->indexOfPath(area()->viewAt(0)->path()), pane,
                          PaneGroup::DropZone::SplitRight);

    QCOMPARE(area()->paneCount(), 2);
    QCOMPARE(area()->paneAt(0)->count(), 1);
    QCOMPARE(area()->paneAt(1)->count(), 1);
    QCOMPARE(area()->paneAt(1)->currentView()->title(), QStringLiteral("甲"));

    // 拖到左緣則插在左邊
    PaneGroup *right = area()->paneAt(1);
    area()->moveTabToPane(right, 0, area()->paneAt(0), PaneGroup::DropZone::SplitLeft);
    QCOMPARE(area()->paneCount(), 2);
    QCOMPARE(area()->paneAt(0)->currentView()->title(), QStringLiteral("甲"));
}

void TestE2eTabs::droppingTabIntoAnotherPaneMovesIt()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));
    area()->setPaneCount(2);
    QCOMPARE(area()->paneAt(0)->count(), 1);
    QCOMPARE(area()->paneAt(1)->count(), 1);

    // 把右格的分頁拖進左格中央 → 併入，右格空掉後自動收掉
    area()->moveTabToPane(area()->paneAt(1), 0, area()->paneAt(0),
                          PaneGroup::DropZone::Into);

    QCOMPARE(area()->paneCount(), 1);
    QCOMPARE(area()->paneAt(0)->count(), 2);
    QCOMPARE(area()->count(), 2);
}

void TestE2eTabs::emptyPaneIsRemovedAutomatically()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));
    area()->setPaneCount(2);
    QCOMPARE(area()->paneCount(), 2);

    // 關掉右格唯一的分頁
    area()->setActiveIndex(1);
    area()->closeActiveTab();

    QCOMPARE(area()->paneCount(), 1);     // 空面板自動收掉
    QCOMPARE(area()->count(), 1);
    QVERIFY(area()->activeView() != nullptr);
}

void TestE2eTabs::activePaneIsMarkedOnlyWhenSplit()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));

    // 只有一格時不需要標示「哪一格是作用中的」
    QCOMPARE(area()->paneCount(), 1);
    QVERIFY(!area()->paneAt(0)->isActiveIndicatorVisible());

    area()->setPaneCount(2);
    QCOMPARE(area()->paneCount(), 2);

    // 分割後，而且只有作用中那一格有標示
    int marked = 0;
    for (int i = 0; i < area()->paneCount(); ++i) {
        PaneGroup *p = area()->paneAt(i);
        QCOMPARE(p->isActiveIndicatorVisible(), p == area()->activeGroup());
        if (p->isActiveIndicatorVisible())
            ++marked;
    }
    QCOMPARE(marked, 1);

    // 切到另一格，標示要跟著移動
    PaneGroup *other = area()->paneAt(0) == area()->activeGroup() ? area()->paneAt(1)
                                                                  : area()->paneAt(0);
    area()->setActiveIndex(area()->paneAt(0) == other ? 0 : area()->count() - 1);
    QVERIFY(other->isActiveIndicatorVisible());

    // 併回一格後標示消失
    area()->setPaneCount(1);
    QVERIFY(!area()->paneAt(0)->isActiveIndicatorVisible());
}

// ------------------------------------------------ 全域操作套用到所有分頁

void TestE2eTabs::themeAppliesToEveryTab()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));

    QAction *black = nullptr;
    for (QAction *a : m_win->findChildren<QAction *>())
        if (a->text() == Theme::name(Theme::Dark))
            black = a;
    QVERIFY(black);
    black->trigger();

    // 非作用中的分頁也要換 —— 否則切回去會看到殘留的舊主題
    for (int i = 0; i < area()->count(); ++i) {
        QCOMPARE(area()->viewAt(i)->theme(), Theme::Dark);
        auto *b = area()->viewAt(i)->findChild<QTextBrowser *>();
        QVERIFY(b);
        QCOMPARE(b->palette().color(QPalette::Base), QColor(Qt::black));
    }
}

void TestE2eTabs::zoomAppliesToEveryTab()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));

    QList<qreal> before;
    for (int i = 0; i < area()->count(); ++i)
        before << area()->viewAt(i)->findChild<QTextBrowser *>()->font().pointSizeF();

    QAction *zoomIn = nullptr;
    for (QAction *a : m_win->findChildren<QAction *>())
        if (a->text() == QStringLiteral("放大"))
            zoomIn = a;
    QVERIFY(zoomIn);
    zoomIn->trigger();

    // 比較模式下各欄字級必須一致，所以縮放要套用到全部分頁
    for (int i = 0; i < area()->count(); ++i) {
        const qreal now = area()->viewAt(i)->findChild<QTextBrowser *>()->font().pointSizeF();
        QVERIFY2(now > before.at(i),
                 qPrintable(QStringLiteral("分頁 %1 沒有放大: %2 → %3")
                                .arg(i).arg(before.at(i)).arg(now)));
    }
}

void TestE2eTabs::dumpScreenshotsIfRequested()
{
    const QString dir = qEnvironmentVariable("MD_E2E_DUMP");
    if (dir.isEmpty())
        QSKIP("未設定 MD_E2E_DUMP，跳過視覺輸出");
    QVERIFY(QDir().mkpath(dir));

    // 用專案自己的文件當語料，內容比合成的豐富
    const QString root = QStringLiteral("/home/angle/local/personal/project/markdown-tool/docs/");
    const QStringList docs{ root + QStringLiteral("sample.md"),
                            root + QStringLiteral("headings.md"),
                            root + QStringLiteral("sample.md") };
    QVERIFY(m_win->openFile(docs.at(0)));
    QVERIFY(m_win->openFile(docs.at(1)));
    QVERIFY(m_win->openFile(make(QStringLiteral("notes.md"), QStringLiteral("第三份文件"), 6)));

    QAction *white = nullptr;
    QAction *black = nullptr;
    for (QAction *a : m_win->findChildren<QAction *>()) {
        if (a->text() == Theme::name(Theme::Light))
            white = a;
        if (a->text() == Theme::name(Theme::Dark))
            black = a;
    }
    QVERIFY(white && black);

    int n = 1;
    for (const bool dark : { false, true }) {
        (dark ? black : white)->trigger();
        for (const int cols : { 1, 2, 3 }) {
            area()->setActiveIndex(0);
            area()->setPaneCount(cols);
            QTest::qWait(200);
            const QString path = QStringLiteral("%1/%2-%3-%4col.png")
                                     .arg(dir)
                                     .arg(n++, 2, 10, QLatin1Char('0'))
                                     .arg(dark ? QStringLiteral("black") : QStringLiteral("white"))
                                     .arg(cols);
            QVERIFY2(m_win->grab().save(path), qPrintable(path));
            qInfo() << "已存" << path;
        }
    }
    white->trigger();
    area()->setPaneCount(1);
}

QTEST_MAIN(TestE2eTabs)
#include "test_e2e_tabs.moc"
