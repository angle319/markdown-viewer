#pragma once

#include "PaneGroup.h"
#include "Theme.h"

#include <QWidget>

class DocumentView;
class MermaidCache;
class QLabel;
class QMenu;
class QSplitter;

/// Container for the split panes — VS Code's editor group model.
///
/// Each PaneGroup has its own tab bar and a document belongs to exactly one of
/// them, which makes "which tab belongs to which pane" self-evident. An earlier
/// design with a single global tab bar could not express that.
///
/// Splits come from two places: choosing a pane count from the menu, or
/// dragging a tab to a pane's left or right edge. Dropping in the middle moves
/// the tab into that pane instead. Empty panes are removed automatically, and
/// at least one always remains.
class DocumentArea : public QWidget
{
    Q_OBJECT

public:
    static constexpr int MaxPanes = 4;

    explicit DocumentArea(MermaidCache *cache, QWidget *parent = nullptr);

    /// Opens a file. An already-open file switches to its tab, wherever it is.
    DocumentView *openFile(const QString &path);

    // ---- Global indexing over all documents: pane order x tab order ----
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

    // ---- Panes ----
    int paneCount() const;
    PaneGroup *paneAt(int index) const;
    /// Number of panes. 1 means no split.
    void setPaneCount(int panes);
    /// Moves the active tab to the neighbouring pane, creating one if needed.
    void moveActiveTabToPane(int delta);

    /// Moves a tab from one pane to another. Into merges, SplitLeft and
    /// SplitRight create a new pane on that side of the target.
    ///
    /// Public so it can be tested: a synthesised cross-widget QDrag sequence
    /// does not run in a test (same reason as MainWindow::openFromUrls).
    /// dropEvent is a thin adapter over this.
    void moveTabToPane(PaneGroup *source, int index, PaneGroup *target,
                       PaneGroup::DropZone zone);

    /// The document currently shown in each pane, in pane order.
    QList<DocumentView *> visibleViews() const;

    /// Closes every tab in the pane except keepIndex.
    void closeOtherTabs(PaneGroup *pane, int keepIndex);
    /// Closes every tab in the pane to the right of fromIndex.
    void closeTabsToTheRight(PaneGroup *pane, int fromIndex);
    /// Closes the whole pane.
    void closePane(PaneGroup *pane);

    /// Builds the tab context menu. The caller owns the result.
    /// Public so tests can inspect the entries and trigger them directly.
    QMenu *buildTabContextMenu(PaneGroup *pane, int index, QWidget *parent = nullptr);

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
    /// Re-lays out after a structural change: equalises pane widths and forces
    /// a repaint.
    void refreshLayout();
    void updatePlaceholder();
    PaneGroup *paneOf(DocumentView *view) const;
    void onTabDropped(PaneGroup *target, PaneGroup::DropZone zone);

    MermaidCache *m_cache = nullptr;   ///< Not owned
    QSplitter *m_splitter = nullptr;
    QLabel *m_placeholder = nullptr;
    PaneGroup *m_activeGroup = nullptr;
    Theme::Mode m_mode = Theme::Light;

    /// Source of a cross-pane drag, recorded on tabDragOut. Only one drag can
    /// be in flight at a time.
    struct DragSource {
        PaneGroup *pane = nullptr;
        int index = -1;
    } m_dragSource;
};
