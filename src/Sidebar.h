#pragma once

#include <QTabWidget>

class FileBrowserPanel;
class TocPanel;

/// 側邊欄：兩個分頁 —— 段落（TOC）與檔案瀏覽。
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
