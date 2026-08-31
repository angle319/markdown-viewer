#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>

class IMermaidRenderer;

/// mermaid 圖的磁碟快取 + 序列化的渲染佇列。
///
/// 快取 key = sha1(source + theme + rendererId)：主題進 key 是因為明暗兩種
/// 主題的產出不同；rendererId 進 key 是因為工具升版後圖形可能改變。
///
/// 佇列刻意限制為「同時只跑一個渲染」：mmdc 每次會拉起一個 headless Chromium
/// （實測峰值約 106MB），一份文件有多張圖時平行跑會把記憶體優勢整個抵銷掉。
class MermaidCache : public QObject
{
    Q_OBJECT

public:
    /// @param renderer 不取得所有權
    explicit MermaidCache(IMermaidRenderer *renderer, QObject *parent = nullptr);

    void setCacheDir(const QString &dir);
    QString cacheDir() const { return m_cacheDir; }

    QString keyFor(const QString &source, bool dark) const;
    QString pathFor(const QString &source, bool dark) const;

    bool isCached(const QString &source, bool dark) const;
    bool rendererAvailable() const;

    /// 已快取或 renderer 不可用時不做事；否則排入佇列。
    /// 重複請求同一個 key 不會排兩次。
    void request(const QString &source, bool dark);

    /// 目前佇列長度（含進行中的那一個），供測試與狀態列使用。
    int pendingCount() const;

Q_SIGNALS:
    /// 一張圖畫好了。path 為快取檔案路徑。
    void rendered(const QString &key, const QString &path);
    void failed(const QString &key, const QString &error);
    /// 佇列清空（所有請求都已完成或失敗）。
    void idle();

private Q_SLOTS:
    void onRendererFinished(bool ok, const QString &error);

private:
    struct Job {
        QString key;
        QString source;
        bool dark = false;
        QString outPath;
    };

    void startNext();

    IMermaidRenderer *m_renderer = nullptr;
    QString m_cacheDir;
    QQueue<Job> m_queue;
    bool m_busy = false;
    Job m_current;
    QHash<QString, bool> m_queued;   ///< key → 已排入，避免重複
};
