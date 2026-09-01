#pragma once

#include <QTabWidget>

class FileBrowserPanel;
class TocPanel;

/// Sidebar with two tabs: the table of contents and the file browser.
class Sidebar : public QTabWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    TocPanel *toc() const { return m_toc; }
    FileBrowserPanel *files() const { return m_files; }

private:
    TocPanel *m_toc = nullptr;
    FileBrowserPanel *m_files = nullptr;
};
