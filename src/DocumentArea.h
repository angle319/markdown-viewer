#pragma once

#include "Theme.h"

#include <QWidget>

class DocumentView;
class MermaidCache;
class QLabel;
class QSplitter;
class QTabBar;

/// 分頁 + 比較模式的容器。
///
/// 構造刻意簡單：**所有 DocumentView 都常駐在同一個 QSplitter 裡**，
/// 靠 setVisible() 決定畫面上出現哪幾個。這樣切換分頁與進出比較模式都不需要
/// 把 widget 在容器之間搬來搬去（reparent 會丟掉捲動位置與焦點）。
///
/// 分頁順序的唯一真實來源是 QTabBar：每個 tab 的 tabData 存著對應的
/// DocumentView 指標，所以使用者拖曳排序後不需要另外同步任何清單。
///
/// 比較模式顯示「從目前分頁起算連續 N 個」；右邊不夠時視窗往左滑，
/// 所以只要分頁數 >= N 就一定顯示滿 N 欄。
class DocumentArea : public QWidget
{
    Q_OBJECT

public:
    static constexpr int MaxCompareColumns = 4;

    explicit DocumentArea(MermaidCache *cache, QWidget *parent = nullptr);

    /// 開檔。已經開過就切到那個分頁，不會重複開。失敗回傳 nullptr。
    DocumentView *openFile(const QString &path);

    int count() const;
    DocumentView *viewAt(int index) const;
    DocumentView *activeView() const;
    int activeIndex() const;
    void setActiveIndex(int index);

    /// 目前開啟的檔案路徑，順序與分頁一致。
    QStringList openPaths() const;

    void closeTab(int index);
    void closeActiveTab();
    void nextTab();
    void previousTab();

    /// 1 = 一般模式；2..4 = 比較模式的欄數。
    void setCompareColumns(int columns);
    int compareColumns() const { return m_compareColumns; }

    /// 目前實際顯示在畫面上的 view（一般模式為 1 個）。
    QList<DocumentView *> visibleViews() const;

    void setTheme(Theme::Mode mode);
    void zoomIn();
    void zoomOut();
    void resetZoom();

Q_SIGNALS:
    /// 作用中的分頁換了（或它的文件換了）
    void activeViewChanged();
    /// 作用中文件的內容／TOC 變了
    void activeDocumentChanged();
    void currentTocIndexChanged(int index);
    void linkActivated(const QUrl &url);
    void statusMessage(const QString &text);
    /// 分頁數量或順序變了（供設定持久化）
    void tabsChanged();

private:
    DocumentView *createView();
    void wire(DocumentView *view);
    void updateVisibility();
    void updateTabText(DocumentView *view);
    int indexOf(DocumentView *view) const;
    int indexOfPath(const QString &path) const;

    MermaidCache *m_cache = nullptr;   ///< 不擁有
    QTabBar *m_tabBar = nullptr;
    QSplitter *m_splitter = nullptr;
    QLabel *m_placeholder = nullptr;
    Theme::Mode m_mode = Theme::Light;
    int m_compareColumns = 1;
};
