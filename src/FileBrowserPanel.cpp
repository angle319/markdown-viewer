#include "FileBrowserPanel.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

FileBrowserPanel::FileBrowserPanel(QWidget *parent)
    : QWidget(parent)
    , m_model(new QFileSystemModel(this))
    , m_view(new QTreeView(this))
    , m_rootLabel(new QLabel(this))
{
    m_model->setNameFilters({ QStringLiteral("*.md"), QStringLiteral("*.markdown"),
                              QStringLiteral("*.mdx"), QStringLiteral("*.mdc"),
                              QStringLiteral("*.mkd"), QStringLiteral("*.txt") });
    m_model->setNameFilterDisables(false);   // Hide non-matching entries rather than greying them
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    m_view->setModel(m_model);
    m_view->setHeaderHidden(true);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setUniformRowHeights(true);
    for (int c = 1; c < m_model->columnCount(); ++c)
        m_view->hideColumn(c);

    auto *up = new QToolButton(this);
    up->setText(QStringLiteral("↑"));
    up->setToolTip(QStringLiteral("上一層"));
    connect(up, &QToolButton::clicked, this, &FileBrowserPanel::goUp);

    auto *choose = new QToolButton(this);
    choose->setText(QStringLiteral("…"));
    choose->setToolTip(QStringLiteral("選擇資料夾"));
    connect(choose, &QToolButton::clicked, this, &FileBrowserPanel::chooseFolder);

    m_rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(4, 4, 4, 4);
    bar->addWidget(up);
    bar->addWidget(m_rootLabel, 1);
    bar->addWidget(choose);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(bar);
    layout->addWidget(m_view);

    // Single click: toggle directories, open files. This panel is itself the
    // way documents get previewed, so a click opening one is the expected thing.
    connect(m_view, &QTreeView::clicked, this, [this](const QModelIndex &idx) {
        const QString path = m_model->filePath(idx);
        if (QFileInfo(path).isDir())
            m_view->setExpanded(idx, !m_view->isExpanded(idx));
        else
            Q_EMIT fileActivated(path);
    });
    connect(m_view, &QTreeView::activated, this, [this](const QModelIndex &idx) {
        const QString path = m_model->filePath(idx);
        if (!QFileInfo(path).isDir())
            Q_EMIT fileActivated(path);
    });
}

void FileBrowserPanel::setRoot(const QString &dir)
{
    const QFileInfo fi(dir);
    if (!fi.isDir())
        return;

    m_root = fi.absoluteFilePath();
    m_model->setRootPath(m_root);
    m_view->setRootIndex(m_model->index(m_root));
    m_rootLabel->setText(fi.fileName().isEmpty() ? m_root : fi.fileName());
    m_rootLabel->setToolTip(m_root);
}

void FileBrowserPanel::selectFile(const QString &path)
{
    const QModelIndex idx = m_model->index(path);
    if (!idx.isValid())
        return;
    m_view->setCurrentIndex(idx);
    m_view->scrollTo(idx, QAbstractItemView::EnsureVisible);
}

void FileBrowserPanel::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("選擇資料夾"), m_root.isEmpty() ? QDir::homePath() : m_root);
    if (!dir.isEmpty())
        setRoot(dir);
}

void FileBrowserPanel::goUp()
{
    if (m_root.isEmpty())
        return;
    QDir d(m_root);
    if (d.cdUp())
        setRoot(d.absolutePath());
}
