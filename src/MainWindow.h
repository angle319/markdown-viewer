#pragma once

#include "Theme.h"
#include "core/Document.h"

#include <QMainWindow>

class FileWatcher;
class IRenderBackend;
class MermaidCache;
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

private Q_SLOTS:
    void onOpenTriggered();
    void onReloadTriggered();
    void onThemeToggled(bool dark);
    void onLinkActivated(const QUrl &url);

private:
    void buildUi();
    void buildMenus();
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

    QAction *m_actSidebar = nullptr;
    QAction *m_actTheme = nullptr;
    QLabel *m_statusRight = nullptr;

    QString m_path;
    QString m_markdown;
    Document m_doc;
    Theme::Mode m_mode = Theme::Light;
    bool m_degradeNoticeShown = false;
};
