#pragma once

#include <QWidget>

#include <QTabBar>

class DocumentView;
class QFrame;
class QRubberBand;
class QStackedWidget;

/// 會偵測「把分頁拖出分頁列」的 QTabBar。
///
/// QTabBar 內建的拖曳只能在同一列內重新排序。要支援拖到別的面板、或拖到邊緣
/// 自動分割，必須在游標離開分頁列時自己發起一個 QDrag。
class PaneTabBar : public QTabBar
{
    Q_OBJECT

public:
    /// 同一程序內的拖曳識別；實際的來源資訊記在 DocumentArea，
    /// 不把指標塞進 mime data。
    static const char *mimeType() { return "application/x-markdown-tool-tab"; }

    using QTabBar::QTabBar;

Q_SIGNALS:
    /// 分頁被拖出分頁列，準備開始跨面板拖曳。
    void tabDragOut(int index);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    QPoint m_pressPos;
    int m_pressedTab = -1;
    bool m_dragging = false;
};

/// 一個分割面板：自己的分頁列 + 自己的文件堆疊。
///
/// 對應 VS Code 的 editor group —— 一份文件屬於某一個面板，面板上方的分頁列
/// 只列出屬於它的文件。這樣「哪個分頁對應哪一格」在畫面上是自明的。
///
/// 文件的所有權在這裡：addView() 收下，takeView() 交出（給另一個面板）。
class PaneGroup : public QWidget
{
    Q_OBJECT

public:
    explicit PaneGroup(QWidget *parent = nullptr);

    int count() const;
    DocumentView *viewAt(int index) const;
    DocumentView *currentView() const;
    int currentIndex() const;
    void setCurrentIndex(int index);

    int indexOf(DocumentView *view) const;
    int indexOfPath(const QString &absolutePath) const;
    QStringList paths() const;

    /// 收下一份文件並新增分頁；回傳它的索引。
    int addView(DocumentView *view);
    /// 交出一份文件（移除分頁但不刪除物件），給另一個面板用。
    DocumentView *takeView(int index);
    /// 移除並刪除。
    void removeView(int index);

    /// 重新整理某份文件的分頁標題（標題來自文件的 H1）。
    void refreshTabText(DocumentView *view);

    /// 作用中的面板會在分頁列下方顯示一條強調色細線。
    /// multiPane 為 false（沒有分割）時不顯示 —— 只有一格時標示它是多餘的。
    void setActive(bool active, bool multiPane);
    bool isActive() const { return m_active; }
    /// 強調線是否正在顯示（供測試驗證）
    bool isActiveIndicatorVisible() const;

    PaneTabBar *tabBar() const { return m_tabBar; }

    /// 拖曳放置區：中間 = 移入這一格，左右兩側 = 在該側新開一格。
    enum class DropZone { Into, SplitLeft, SplitRight };
    static DropZone zoneFor(const QRect &paneRect, const QPoint &pos);

Q_SIGNALS:
    /// 使用者在這個面板裡操作了（點分頁、點內容），應該把它設為作用中面板。
    void activated();
    void currentChanged();
    void tabsChanged();
    void closeRequested(int index);
    /// 分頁被拖出這個面板的分頁列
    void tabDragOut(int index);
    /// 有分頁被放到這個面板上
    void tabDropped(PaneGroup *target, PaneGroup::DropZone zone);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    void showDropHint(DropZone zone);

    PaneTabBar *m_tabBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QFrame *m_activeLine = nullptr;
    QRubberBand *m_dropHint = nullptr;
    bool m_active = false;
};
