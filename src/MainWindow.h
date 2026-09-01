#pragma once

#include "Theme.h"

#include <QMainWindow>

class DocumentArea;
class DocumentView;
class MermaidCache;
class MmdcRenderer;
class PathBar;
class Sidebar;
class QAction;
class QActionGroup;
class QLabel;
class QSplitter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Opens a file. An already-open file switches to its existing tab.
    bool openFile(const QString &path);

    /// The active document, or nullptr when no tab is open.
    DocumentView *activeView() const;
    DocumentArea *area() const { return m_area; }

    /// Picks the first usable local path from a set of dropped URLs: a markdown
    /// file if there is one, otherwise a directory. Pure logic, public so it can
    /// be unit tested.
    static QString firstUsablePath(const QList<QUrl> &urls);

    /// Handles a set of dropped URLs: a markdown file is opened, a directory
    /// re-roots the sidebar. Returns whether anything was handled.
    ///
    /// Separate from dropEvent so it can be tested: a synthesised QDropEvent
    /// never reaches the handler in a test, because QWidget::event() is
    /// protected and QApplication::notify routes drag and drop through
    /// QDragManager. dropEvent is a thin adapter over this.
    ///
    /// 中：合成 QDropEvent 在測試裡送不到，所以邏輯抽在這裡。
    bool openFromUrls(const QList<QUrl> &urls);

protected:
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

public Q_SLOTS:
    /// Path bar submitted: open a file, re-root on a directory, or report a
    /// missing path in the status bar.
    void onPathSubmitted(const QString &path);

private Q_SLOTS:
    void onOpenTriggered();
    void onReloadTriggered();
    void onLinkActivated(const QUrl &url);
    void focusPathBar();
    /// The active tab changed; title, path bar and TOC must follow
    void syncToActiveView();

private:
    void buildUi();
    void buildMenus();
    void setMode(Theme::Mode mode);
    void setCompareColumns(int columns);
    void loadSettings();
    void saveSettings();
    void updateStatus(const QString &transient = QString());

    Sidebar *m_sidebar = nullptr;
    QSplitter *m_splitter = nullptr;
    DocumentArea *m_area = nullptr;
    MmdcRenderer *m_renderer = nullptr;
    MermaidCache *m_cache = nullptr;

    PathBar *m_pathBar = nullptr;
    QAction *m_actSidebar = nullptr;
    QAction *m_actWhite = nullptr;
    QAction *m_actBlack = nullptr;
    QActionGroup *m_compareGroup = nullptr;
    QLabel *m_statusRight = nullptr;

    Theme::Mode m_mode = Theme::Light;
    bool m_themeApplied = false;   ///< Forces one unconditional apply at startup
};
