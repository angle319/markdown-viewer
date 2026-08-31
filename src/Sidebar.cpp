#include "Sidebar.h"

#include "FileBrowserPanel.h"
#include "TocPanel.h"

Sidebar::Sidebar(QWidget *parent)
    : QTabWidget(parent)
    , m_toc(new TocPanel(this))
    , m_files(new FileBrowserPanel(this))
{
    setDocumentMode(true);
    setTabPosition(QTabWidget::North);
    addTab(m_toc, QStringLiteral("段落"));
    addTab(m_files, QStringLiteral("檔案"));
}
