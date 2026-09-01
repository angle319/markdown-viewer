#pragma once

#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

/// Watches a single file for changes, with debouncing.
///
/// Handles the atomic save most editors perform (write a temporary file, then
/// rename over the original): the original inode is replaced, so
/// QFileSystemWatcher silently stops watching and the path must be re-added.
class FileWatcher : public QObject
{
    Q_OBJECT

public:
    explicit FileWatcher(QObject *parent = nullptr);

    void watch(const QString &path);
    void stop();
    QString path() const { return m_path; }

    void setDebounceMs(int ms);

Q_SIGNALS:
    void fileChanged(const QString &path);

private:
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QString m_path;
};
