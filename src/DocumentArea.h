#pragma once

#include "PaneGroup.h"
#include "Theme.h"

#include <QWidget>

class DocumentView;
class MermaidCache;
class QLabel;
class QSplitter;

/// 分割面板的容器 —— VS Code 的 editor group 模型。
///
/// 每個 PaneGroup 有自己的分頁列，一份文件屬於其中一格。這樣「哪個分頁對應
/// 哪一格」在畫面上是自明的；先前用單一全域分頁列的版本做不到這件事。
///
/// 分割的產生方式有兩種：選單選欄數，或把分頁拖到某一格的左右邊緣。
/// 拖到中間則是移入該格。空掉的面板會自動收掉（至少保留一格）。
class DocumentArea : public QWidget
{
    Q_OBJECT

public:
    static constexpr int MaxPanes = 4;

    explicit DocumentArea(MermaidCache *cache, QWidget *parent = nullptr);

    /// 開檔。已經開過就切到那個分頁（可能在別的面板），不會重複開。
    DocumentView *openFile(const QString &path);

    // ---- 以「全域索引」看待所有文件：面板順序 × 面板內分頁順序 ----
    int count() const;
    DocumentView *viewAt(int index) const;
    QStringList openPaths() const;
    int activeIndex() const;
    void setActiveIndex(int index);

    DocumentView *activeView() const;
    PaneGroup *activeGroup() const { return m_activeGroup; }

    void closeTab(int index);
    void closeActiveTab();
    void nextTab();
    void previousTab();

    // ---- 面板 ----
    int paneCount() const;
    PaneGroup *paneAt(int index) const;
    /// 面板數量。1 = 沒有分割。
    void setPaneCount(int panes);
    /// 把作用中的分頁搬到相鄰面板；沒有相鄰面板時新建一個。
    void moveActiveTabToPane(int delta);

    /// 把某一格的某個分頁搬到另一格。zone 為 Into 是併入，
    /// SplitLeft/SplitRight 則在目標格的該側新開一格。
    ///
    /// 公開是為了可測：合成跨 widget 的 QDrag 序列在測試裡跑不起來
    /// （同 MainWindow::openFromUrls 的理由）。dropEvent 只是這個方法的轉接。
    void moveTabToPane(PaneGroup *source, int index, PaneGroup *target,
                       PaneGroup::DropZone zone);

    /// 目前每一格顯示的文件，依面板順序。
    QList<DocumentView *> visibleViews() const;

    void setTheme(Theme::Mode mode);
    void zoomIn();
    void zoomOut();
    void resetZoom();

Q_SIGNALS:
    void activeViewChanged();
    void activeDocumentChanged();
    void currentTocIndexChanged(int index);
    void linkActivated(const QUrl &url);
    void statusMessage(const QString &text);
    void tabsChanged();

private:
    DocumentView *createView();
    void wireView(DocumentView *view);
    PaneGroup *createPane(int at = -1);
    void wirePane(PaneGroup *pane);
    void setActivePane(PaneGroup *pane);
    void pruneEmptyPanes();
    void refreshPaneIndicators();
    /// 結構變動後重新配置：平均分配面板寬度並強制重繪。
    void refreshLayout();
    void updatePlaceholder();
    PaneGroup *paneOf(DocumentView *view) const;
    void onTabDropped(PaneGroup *target, PaneGroup::DropZone zone);

    MermaidCache *m_cache = nullptr;   ///< 不擁有
    QSplitter *m_splitter = nullptr;
    QLabel *m_placeholder = nullptr;
    PaneGroup *m_activeGroup = nullptr;
    Theme::Mode m_mode = Theme::Light;

    /// 跨面板拖曳的來源，在 tabDragOut 時記下（同一時間只會有一個拖曳）
    struct DragSource {
        PaneGroup *pane = nullptr;
        int index = -1;
    } m_dragSource;
};
