#pragma once

#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

/// 監看單一檔案的變更，帶去彈跳（debounce）。
///
/// 處理編輯器的 atomic save：多數編輯器是「寫暫存檔 → rename 覆蓋」，
/// 原 inode 被換掉後 QFileSystemWatcher 會失去監看，必須重新 addPath。
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
