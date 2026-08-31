#include "core/FileWatcher.h"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

FileWatcher::FileWatcher(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_debounce(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        m_debounce->start();
    });

    connect(m_debounce, &QTimer::timeout, this, [this] {
        if (m_path.isEmpty())
            return;

        // atomic save 之後原路徑已不在監看清單裡，補回去
        if (!m_watcher->files().contains(m_path) && QFileInfo::exists(m_path))
            m_watcher->addPath(m_path);

        if (QFileInfo::exists(m_path))
            Q_EMIT fileChanged(m_path);
    });
}

void FileWatcher::setDebounceMs(int ms)
{
    m_debounce->setInterval(ms);
}

void FileWatcher::watch(const QString &path)
{
    stop();
    m_path = path;
    if (!path.isEmpty() && QFileInfo::exists(path))
        m_watcher->addPath(path);
}

void FileWatcher::stop()
{
    m_debounce->stop();
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    m_path.clear();
}
