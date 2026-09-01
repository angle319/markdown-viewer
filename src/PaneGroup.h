#pragma once

#include <QWidget>

#include "Theme.h"

#include <QTabBar>

class DocumentView;
class QFrame;
class QRubberBand;
class QStackedWidget;

/// A QTabBar that detects a tab being dragged out of the bar.
///
/// QTabBar's built-in drag only reorders within the same bar. Dragging to
/// another pane, or to an edge to split, requires starting a QDrag by hand once
/// the cursor leaves the bar.
class PaneTabBar : public QTabBar
{
    Q_OBJECT

public:
    /// Marks an in-process tab drag. The actual source is recorded in
    /// DocumentArea rather than smuggled through the mime data as a pointer.
    static const char *mimeType() { return "application/x-markdown-tool-tab"; }

    using QTabBar::QTabBar;

    /// Upper bound on a single tab's width, in logical pixels.
    ///
    /// setElideMode() alone is not enough: Qt only elides once the whole bar
    /// overflows, so a single long title still stretches its tab across the
    /// pane. Capping tabSizeHint() makes the elide kick in per tab.
    ///
    /// 中：單靠 setElideMode 不夠，Qt 只在整條分頁列塞不下時才截斷。
    static constexpr int MaxTabWidth = 220;
    static constexpr int MinTabWidth = 80;

Q_SIGNALS:
    /// A tab was dragged out of the bar; a cross-pane drag is starting.
    void tabDragOut(int index);

protected:
    QSize tabSizeHint(int index) const override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    QPoint m_pressPos;
    int m_pressedTab = -1;
    bool m_dragging = false;
};

/// One split pane: its own tab bar over its own stack of documents.
///
/// This is VS Code's editor group — a document belongs to exactly one pane and
/// the bar above a pane lists only that pane's documents, which is what makes
/// "which tab belongs to which pane" self-evident on screen.
///
/// Document ownership lives here: addView() takes it, takeView() hands it to
/// another pane.
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

    /// Takes ownership of a document and adds a tab; returns its index.
    int addView(DocumentView *view);
    /// Gives up a document (removes the tab without deleting) for another pane.
    DocumentView *takeView(int index);
    /// Removes and deletes.
    void removeView(int index);

    /// Refreshes a document's tab text (the title comes from its H1).
    void refreshTabText(DocumentView *view);

    /// The active pane shows a thin accent line under its tab bar.
    /// With multiPane false (no split) it is hidden — marking the only pane is
    /// pointless.
    void applyTheme(Theme::Mode mode);
    void setActive(bool active, bool multiPane);
    bool isActive() const { return m_active; }
    /// Whether the accent line is currently shown (for tests)
    bool isActiveIndicatorVisible() const;

    PaneTabBar *tabBar() const { return m_tabBar; }

    /// Drop zones: the middle merges into this pane, the sides split there.
    enum class DropZone { Into, SplitLeft, SplitRight };
    static DropZone zoneFor(const QRect &paneRect, const QPoint &pos);

Q_SIGNALS:
    /// The user interacted with this pane; it should become the active one.
    void activated();
    void currentChanged();
    void tabsChanged();
    void closeRequested(int index);
    /// A tab was dragged out of this pane's bar
    void tabDragOut(int index);
    /// A tab was dropped on this pane
    void tabDropped(PaneGroup *target, PaneGroup::DropZone zone);
    /// A tab was right-clicked
    void tabContextMenuRequested(int index, const QPoint &globalPos);

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
