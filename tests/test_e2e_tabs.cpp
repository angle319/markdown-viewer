#include <QtTest>

#include <algorithm>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
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

#ifndef SAMPLE_MD
#  error "SAMPLE_MD not defined (CMake should supply the path to docs/sample.md)"
#endif
#ifndef HEADINGS_MD
#  error "HEADINGS_MD not defined (CMake should supply the path to docs/headings.md)"
#endif

/// Multiple documents: tabs and split panes.
class TestE2eTabs : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // --- Tabs ---
    void openingTwoFilesCreatesTwoTabs();
    void reopeningSameFileSwitchesInsteadOfDuplicating();
    void tabTitleComesFromDocumentHeading();
    void switchingTabUpdatesPathBarAndToc();
    void closingTabRemovesItAndKeepsOthers();
    void closingLastTabShowsEmptyState();
    void tabsCanBeReordered();
    void longTabTitlesAreCapped();
    void eachTabWatchesItsOwnFile();
    void linkNavigationStaysInSameTab();

    // --- Split panes (VS Code style editor groups) ---
    void splittingGivesEachPaneItsOwnTabBar();
    void paneCountNeverExceedsDocumentCount();
    void mergingPanesBringsDocumentsBack();
    void movingTabToAdjacentPaneCreatesOne();
    void dropZoneIsComputedFromPosition();
    void droppingTabOnEdgeSplits();
    void droppingTabIntoAnotherPaneMovesIt();
    void emptyPaneIsRemovedAutomatically();
    void activePaneIsMarkedOnlyWhenSplit();
    void paneGeometryStaysSaneAfterMoves();
    void panesGetComparableWidths();

    // --- Tab context menu ---
    void contextMenuOffersCloseOptions();
    void closeOthersLeavesOnlyThatTab();
    void closeToTheRightKeepsLeftSide();
    void closePaneClosesAllItsTabs();

    // --- Whole-area actions reach every tab ---
    void themeAppliesToEveryTab();
    void zoomAppliesToEveryTab();

    // --- Optional visual output ---
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

// ----------------------------------------------------------------- Tabs

void TestE2eTabs::openingTwoFilesCreatesTwoTabs()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("甲"))));
    QCOMPARE(area()->count(), 1);

    QVERIFY(m_win->openFile(make(QStringLiteral("b.md"), QStringLiteral("乙"))));
    QCOMPARE(area()->count(), 2);
    QCOMPARE(area()->activeIndex(), 1);
    QCOMPARE(tabBar()->count(), 2);

    // The two tabs hold two distinct documents
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

    // A non-canonical path must be recognised as the same file
    QVERIFY(m_win->openFile(m_dir->path() + QStringLiteral("/./a.md")));
    QCOMPARE(area()->count(), 2);
}

void TestE2eTabs::tabTitleComesFromDocumentHeading()
{
    QVERIFY(m_win->openFile(make(QStringLiteral("a.md"), QStringLiteral("我的標題"))));
    QCOMPARE(tabBar()->tabText(0), QStringLiteral("我的標題"));
    QCOMPARE(tabBar()->tabToolTip(0), area()->viewAt(0)->path());

    // With no H1 the file name is used
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

    // The empty-state hint must be visible
    bool sawPlaceholder = false;
    for (QLabel *l : area()->findChildren<QLabel *>())
        if (l->isVisible() && l->text().contains(QStringLiteral("沒有開啟任何檔案")))
            sawPlaceholder = true;
    QVERIFY2(sawPlaceholder, "關掉最後一個分頁後沒有空狀態提示");

    // Opening still works afterwards
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

void TestE2eTabs::longTabTitlesAreCapped()
{
    // A very long H1 used to stretch its tab right across the pane.
    // setElideMode() alone does not help: Qt elides only once the whole bar
    // overflows, so a single long tab keeps its full width.
    // 中：單一長標題會把分頁撐爆，要蓋 tabSizeHint。
    const QString longTitle =
        QStringLiteral("一個刻意很長的標題 — 長到足以撐爆分頁 — 用來驗證寬度上限有生效");
    QVERIFY(m_win->openFile(make(QStringLiteral("long.md"), longTitle)));
    QVERIFY(m_win->openFile(make(QStringLiteral("s.md"), QStringLiteral("短"))));

    PaneGroup *pane = area()->paneAt(0);
    QTabBar *bar = pane->tabBar();
    QCOMPARE(bar->tabText(0), longTitle);          // the model keeps the full text

    const int longWidth = bar->tabRect(0).width();
    const int shortWidth = bar->tabRect(1).width();
    qInfo() << "long tab" << longWidth << "px, short tab" << shortWidth << "px";

    QVERIFY2(longWidth <= PaneTabBar::MaxTabWidth,
             qPrintable(QStringLiteral("long tab is %1px, cap is %2px")
                            .arg(longWidth).arg(PaneTabBar::MaxTabWidth)));
    // A short title must still get a short tab — the cap is a maximum, not a fixed width
    QVERIFY2(shortWidth < longWidth,
             qPrintable(QStringLiteral("short tab %1px is not narrower than long tab %2px")
                            .arg(shortWidth).arg(longWidth)));
    QVERIFY(shortWidth >= PaneTabBar::MinTabWidth);

    // The full title stays reachable through the tooltip
    QCOMPARE(bar->tabToolTip(0), area()->viewAt(0)->path());
}

void TestE2eTabs::eachTabWatchesItsOwnFile()
{
    const QString a = make(QStringLiteral("a.md"), QStringLiteral("甲"), 2);
    const QString b = make(QStringLiteral("b.md"), QStringLiteral("乙"), 2);
    QVERIFY(m_win->openFile(a));
    QVERIFY(m_win->openFile(b));
    area()->setActiveIndex(1);             // 作用中是「乙」

    // Edit the first document's file: it must reload even though its tab is
    // not the active one
    QFile f(a);
    QVERIFY(f.open(QIODevice::Append));
    f.write(QStringLiteral("\n## 甲 的第 3 節\n\n新內容。\n").toUtf8());
    f.close();

    QTRY_VERIFY_WITH_TIMEOUT(area()->viewAt(0)->document().toc.size() == 4, 8000);
    // The active tab is unaffected
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

    // The point: the file changes within the same tab, no new tab is added.
    // An index page with dozens of links would otherwise spawn dozens of tabs.
    QTRY_COMPARE(area()->count(), 1);
    QCOMPARE(area()->activeView()->title(), QStringLiteral("目標"));
    QCOMPARE(tabBar()->tabText(0), QStringLiteral("目標"));
    QCOMPARE(m_win->findChild<PathBar *>()->path(), QFileInfo(target).absoluteFilePath());
}

// --------------------------------------------------------- Split panes

void TestE2eTabs::splittingGivesEachPaneItsOwnTabBar()
{
    for (const QString &n : { QStringLiteral("a.md"), QStringLiteral("b.md"),
                              QStringLiteral("c.md") })
        QVERIFY(m_win->openFile(make(n, n.left(1).toUpper())));
    QCOMPARE(area()->count(), 3);
    QCOMPARE(area()->paneCount(), 1);

    area()->setPaneCount(2);
    QCOMPARE(area()->paneCount(), 2);

    // The point: each pane has its own tab bar listing exactly its own
    // documents. The earlier single global bar could not express that — there
    // was no way to tell which tab belonged to which pane.
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

    // Even a narrow pane must keep a usable edge zone
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

    // Drop the first document on the right edge of its own pane, creating one
    // on that side. A real QDrag sequence does not run in a test, so this
    // exercises the method dropEvent adapts to.
    area()->moveTabToPane(pane, pane->indexOfPath(area()->viewAt(0)->path()), pane,
                          PaneGroup::DropZone::SplitRight);

    QCOMPARE(area()->paneCount(), 2);
    QCOMPARE(area()->paneAt(0)->count(), 1);
    QCOMPARE(area()->paneAt(1)->count(), 1);
    QCOMPARE(area()->paneAt(1)->currentView()->title(), QStringLiteral("甲"));

    // Dropping on the left edge inserts on the left
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

    // Drop the right pane's tab into the middle of the left pane: it merges,
    // and the now-empty right pane is removed
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

    // Close the right pane's only tab
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

    // With a single pane there is nothing to mark as active
    QCOMPARE(area()->paneCount(), 1);
    QVERIFY(!area()->paneAt(0)->isActiveIndicatorVisible());

    area()->setPaneCount(2);
    QCOMPARE(area()->paneCount(), 2);

    // After splitting, exactly the active pane is marked
    int marked = 0;
    for (int i = 0; i < area()->paneCount(); ++i) {
        PaneGroup *p = area()->paneAt(i);
        QCOMPARE(p->isActiveIndicatorVisible(), p == area()->activeGroup());
        if (p->isActiveIndicatorVisible())
            ++marked;
    }
    QCOMPARE(marked, 1);

    // Switching panes moves the marker
    PaneGroup *other = area()->paneAt(0) == area()->activeGroup() ? area()->paneAt(1)
                                                                  : area()->paneAt(0);
    area()->setActiveIndex(area()->paneAt(0) == other ? 0 : area()->count() - 1);
    QVERIFY(other->isActiveIndicatorVisible());

    // Merging back to one pane clears it
    area()->setPaneCount(1);
    QVERIFY(!area()->paneAt(0)->isActiveIndicatorVisible());
}

/// Invariants that must hold after every structural change:
///  - every DocumentView lives in some pane's stack (never orphaned, never
///    parented straight onto the PaneGroup)
///  - only each pane's current document is visible
///  - a visible document never overlaps the tab bar
static void checkPaneInvariants(DocumentArea *area, const char *where)
{
    for (int i = 0; i < area->paneCount(); ++i) {
        PaneGroup *pane = area->paneAt(i);
        QVERIFY2(pane, where);

        for (int t = 0; t < pane->count(); ++t) {
            DocumentView *v = pane->viewAt(t);
            QVERIFY2(v, where);

            // Must be a descendant of this pane, and not a direct child of it
            QVERIFY2(pane->isAncestorOf(v),
                     qPrintable(QStringLiteral("%1: 文件不在面板裡").arg(QLatin1String(where))));
            QVERIFY2(v->parentWidget() != pane,
                     qPrintable(QStringLiteral("%1: 文件直接掛在 PaneGroup 上，"
                                               "會蓋到分頁列").arg(QLatin1String(where))));

            const bool shouldShow = (t == pane->currentIndex());
            QVERIFY2(v->isVisibleTo(pane) == shouldShow,
                     qPrintable(QStringLiteral("%1: 面板 %2 的分頁 %3 可見性不對")
                                    .arg(QLatin1String(where)).arg(i).arg(t)));

            if (shouldShow) {
                // Content must sit below the tab bar.
                // Mind the coordinate systems: v->geometry() is relative to its
                // parent (the QStackedWidget) while the tab bar's geometry is
                // relative to the PaneGroup, so map into one space first.
                const int contentTop = v->mapTo(pane, QPoint(0, 0)).y();
                const int tabBottom = pane->tabBar()->geometry().bottom();
                QVERIFY2(contentTop >= tabBottom,
                         qPrintable(QStringLiteral("%1: 面板 %2 的內容蓋到分頁列 "
                                                   "(content top=%3, tabbar bottom=%4)")
                                        .arg(QLatin1String(where)).arg(i)
                                        .arg(contentTop).arg(tabBottom)));
                QVERIFY2(v->width() <= pane->width(),
                         qPrintable(QStringLiteral("%1: 面板 %2 的內容比面板寬")
                                        .arg(QLatin1String(where)).arg(i)));
            }
        }
    }
}

void TestE2eTabs::paneGeometryStaysSaneAfterMoves()
{
    for (const QString &n : { QStringLiteral("a.md"), QStringLiteral("b.md"),
                              QStringLiteral("c.md") })
        QVERIFY(m_win->openFile(make(n, n.left(1).toUpper())));
    QTest::qWait(50);
    checkPaneInvariants(area(), "開三個分頁後");

    area()->setPaneCount(2);
    QTest::qWait(50);
    checkPaneInvariants(area(), "分割成 2 格後");

    area()->setPaneCount(3);
    QTest::qWait(50);
    checkPaneInvariants(area(), "分割成 3 格後");

    // Drag moves — the path that actually produced the broken layout
    area()->moveTabToPane(area()->paneAt(2), 0, area()->paneAt(0),
                          PaneGroup::DropZone::Into);
    QTest::qWait(50);
    checkPaneInvariants(area(), "把分頁併入別格後");

    area()->moveTabToPane(area()->paneAt(0), 0, area()->paneAt(1),
                          PaneGroup::DropZone::SplitRight);
    QTest::qWait(50);
    checkPaneInvariants(area(), "拖到邊緣分割後");

    // A drag-created pane must also get a sensible width. Measured: the new
    // pane came out around 150 px, narrow enough to wrap tables and inline code
    // one character per line.
    for (int i = 0; i < area()->paneCount(); ++i)
        QVERIFY2(area()->paneAt(i)->width() > 200,
                 qPrintable(QStringLiteral("拖曳分割後面板 %1 只有 %2px")
                                .arg(i).arg(area()->paneAt(i)->width())));

    area()->setPaneCount(1);
    QTest::qWait(50);
    checkPaneInvariants(area(), "併回單格後");
}

void TestE2eTabs::panesGetComparableWidths()
{
    for (const QString &n : { QStringLiteral("a.md"), QStringLiteral("b.md"),
                              QStringLiteral("c.md") })
        QVERIFY(m_win->openFile(make(n, n.left(1).toUpper())));

    area()->setPaneCount(3);
    QTest::qWait(50);

    QList<int> widths;
    for (int i = 0; i < area()->paneCount(); ++i)
        widths << area()->paneAt(i)->width();

    const int maxW = *std::max_element(widths.begin(), widths.end());
    const int minW = *std::min_element(widths.begin(), widths.end());
    qInfo() << "面板寬度" << widths;

    // A newly split pane must not be squeezed to a sliver; measured, content
    // wrapped one character per line
    QVERIFY2(minW > 200,
             qPrintable(QStringLiteral("最窄的面板只有 %1px").arg(minW)));
    QVERIFY2(maxW <= minW * 2,
             qPrintable(QStringLiteral("面板寬度差太多: %1 vs %2").arg(minW).arg(maxW)));
}

void TestE2eTabs::contextMenuOffersCloseOptions()
{
    for (const QString &n : { QStringLiteral("a.md"), QStringLiteral("b.md"),
                              QStringLiteral("c.md") })
        QVERIFY(m_win->openFile(make(n, n.left(1).toUpper())));

    PaneGroup *pane = area()->paneAt(0);
    QScopedPointer<QMenu> menu(area()->buildTabContextMenu(pane, 1));
    QVERIFY(menu);

    QStringList texts;
    for (QAction *a : menu->actions())
        if (!a->isSeparator())
            texts << a->text();
    QCOMPARE(texts, QStringList({ QStringLiteral("關閉"), QStringLiteral("關閉其他"),
                                  QStringLiteral("關閉右側全部"), QStringLiteral("關閉這一格"),
                                  QStringLiteral("移到右邊面板"),
                                  QStringLiteral("移到左邊面板") }));

    // Disabled states must make sense: with one pane, closing it is not offered
    const auto action = [&](const QString &t) -> QAction * {
        for (QAction *a : menu->actions())
            if (a->text() == t)
                return a;
        return nullptr;
    };
    QVERIFY(action(QStringLiteral("關閉其他"))->isEnabled());          // 有 3 個分頁
    QVERIFY(action(QStringLiteral("關閉右側全部"))->isEnabled());       // index 1，右邊還有
    QVERIFY(!action(QStringLiteral("關閉這一格"))->isEnabled());        // 只有一格

    // The last tab has nothing to its right to close
    QScopedPointer<QMenu> last(area()->buildTabContextMenu(pane, 2));
    for (QAction *a : last->actions())
        if (a->text() == QStringLiteral("關閉右側全部"))
            QVERIFY(!a->isEnabled());
}

void TestE2eTabs::closeOthersLeavesOnlyThatTab()
{
    for (const QString &n : { QStringLiteral("a.md"), QStringLiteral("b.md"),
                              QStringLiteral("c.md") })
        QVERIFY(m_win->openFile(make(n, n.left(1).toUpper())));
    QCOMPARE(area()->count(), 3);

    PaneGroup *pane = area()->paneAt(0);
    area()->closeOtherTabs(pane, 1);          // 留下「B」

    QCOMPARE(area()->count(), 1);
    QCOMPARE(area()->activeView()->title(), QStringLiteral("B"));
    QCOMPARE(pane->tabBar()->count(), 1);
}

void TestE2eTabs::closeToTheRightKeepsLeftSide()
{
    for (const QString &n : { QStringLiteral("a.md"), QStringLiteral("b.md"),
                              QStringLiteral("c.md"), QStringLiteral("d.md") })
        QVERIFY(m_win->openFile(make(n, n.left(1).toUpper())));
    QCOMPARE(area()->count(), 4);

    PaneGroup *pane = area()->paneAt(0);
    area()->closeTabsToTheRight(pane, 1);     // 留下 A、B

    QCOMPARE(area()->count(), 2);
    QStringList titles;
    for (int i = 0; i < area()->count(); ++i)
        titles << area()->viewAt(i)->title();
    QCOMPARE(titles, QStringList({ QStringLiteral("A"), QStringLiteral("B") }));
}

void TestE2eTabs::closePaneClosesAllItsTabs()
{
    for (const QString &n : { QStringLiteral("a.md"), QStringLiteral("b.md"),
                              QStringLiteral("c.md") })
        QVERIFY(m_win->openFile(make(n, n.left(1).toUpper())));
    area()->setPaneCount(2);
    QCOMPARE(area()->paneCount(), 2);

    const int inSecondPane = area()->paneAt(1)->count();
    QVERIFY(inSecondPane >= 1);

    area()->closePane(area()->paneAt(1));

    QCOMPARE(area()->paneCount(), 1);          // 空格自動收掉
    QCOMPARE(area()->count(), 3 - inSecondPane);
    QVERIFY(area()->activeView() != nullptr);
}

// --------------------------------------- Whole-area actions reach every tab

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

    // Inactive tabs must change too, or switching back shows the old theme
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
        before << area()->viewAt(i)->findChild<QTextBrowser *>()
                      ->document()->defaultFont().pointSizeF();

    QAction *zoomIn = nullptr;
    for (QAction *a : m_win->findChildren<QAction *>())
        if (a->text() == QStringLiteral("放大"))
            zoomIn = a;
    QVERIFY(zoomIn);
    zoomIn->trigger();

    // Split panes must share a font size, so zoom applies to every tab
    for (int i = 0; i < area()->count(); ++i) {
        const qreal now = area()->viewAt(i)->findChild<QTextBrowser *>()
                              ->document()->defaultFont().pointSizeF();
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

    // Use the project's own documents; they are richer than synthetic ones
    const QStringList docs{ QString::fromUtf8(SAMPLE_MD),
                            QString::fromUtf8(HEADINGS_MD),
                            QString::fromUtf8(SAMPLE_MD) };
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
