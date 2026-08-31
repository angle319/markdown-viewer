#pragma once

#include "Theme.h"
#include "core/Document.h"

#include <QMainWindow>

class FileWatcher;
class IRenderBackend;
class MermaidCache;
class PathBar;
class MmdcRenderer;
class Sidebar;
class QAction;
class QLabel;
class QSplitter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *e) override;

public Q_SLOTS:
    /// 路徑列送出：檔案就開啟、資料夾就換側邊欄根目錄、不存在就在狀態列說明。
    void onPathSubmitted(const QString &path);

private Q_SLOTS:
    void onOpenTriggered();
    void onReloadTriggered();
    void onLinkActivated(const QUrl &url);
    void focusPathBar();

private:
    void buildUi();
    void buildMenus();
    void setMode(Theme::Mode mode);
    void reparse(bool preserveScroll);
    void loadSettings();
    void saveSettings();
    void updateStatus(const QString &transient = QString());

    Sidebar *m_sidebar = nullptr;
    QSplitter *m_splitter = nullptr;
    IRenderBackend *m_backend = nullptr;
    MmdcRenderer *m_renderer = nullptr;
    MermaidCache *m_cache = nullptr;
    FileWatcher *m_watcher = nullptr;

    PathBar *m_pathBar = nullptr;
    QAction *m_actSidebar = nullptr;
    QAction *m_actWhite = nullptr;
    QAction *m_actBlack = nullptr;
    QLabel *m_statusRight = nullptr;

    QString m_path;
    QString m_markdown;
    Document m_doc;
    Theme::Mode m_mode = Theme::Light;
    bool m_degradeNoticeShown = false;
};
