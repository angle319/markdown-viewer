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

    /// 開檔。已開過的檔案會切到既有分頁，不會重複開。
    bool openFile(const QString &path);

    /// 作用中的分頁；沒有任何分頁時回傳 nullptr。
    DocumentView *activeView() const;
    DocumentArea *area() const { return m_area; }

    /// 從一組拖入的 URL 挑出第一個能處理的本機路徑（markdown 檔優先，其次資料夾）。
    /// 純邏輯，公開以便單元測試。
    static QString firstUsablePath(const QList<QUrl> &urls);

    /// 處理一組拖入的 URL：markdown 檔就開、資料夾就換側邊欄的根。
    /// 回傳是否有東西被處理。
    ///
    /// 與 dropEvent 分開是為了可測 —— 合成 QDropEvent 送給 widget 在測試裡
    /// 走不到（QWidget::event 是 protected，而 QApplication::notify 對拖放
    /// 另有一套經過 QDragManager 的流程）。dropEvent 只是這個方法的薄轉接。
    bool openFromUrls(const QList<QUrl> &urls);

protected:
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

public Q_SLOTS:
    /// 路徑列送出：檔案就開啟、資料夾就換側邊欄根目錄、不存在就在狀態列說明。
    void onPathSubmitted(const QString &path);

private Q_SLOTS:
    void onOpenTriggered();
    void onReloadTriggered();
    void onLinkActivated(const QUrl &url);
    void focusPathBar();
    /// 作用中分頁換了：標題、路徑列、TOC 都要跟上
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
    bool m_themeApplied = false;   ///< 啟動時要無條件套用一次
};
