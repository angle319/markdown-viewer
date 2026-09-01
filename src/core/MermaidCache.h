#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>

class IMermaidRenderer;

/// On-disk cache for mermaid diagrams, plus a serialised render queue.
///
/// Cache key = sha1(source + theme + rendererId). Theme is part of the key
/// because light and dark output differ; rendererId is part of it because a
/// tool upgrade can change the drawing.
///
/// The queue deliberately allows only one render at a time: each mmdc run
/// starts a headless Chromium (measured peak around 106 MB), so rendering a
/// document's diagrams in parallel would cancel out the memory advantage this
/// whole design exists for.
class MermaidCache : public QObject
{
    Q_OBJECT

public:
    /// @param renderer Not owned
    explicit MermaidCache(IMermaidRenderer *renderer, QObject *parent = nullptr);

    void setCacheDir(const QString &dir);
    QString cacheDir() const { return m_cacheDir; }

    QString keyFor(const QString &source, bool dark) const;
    QString pathFor(const QString &source, bool dark) const;

    bool isCached(const QString &source, bool dark) const;
    bool rendererAvailable() const;

    /// See IMermaidRenderer::outputScale()
    qreal outputScale() const;

    /// Does nothing when already cached or the renderer is unavailable;
    /// otherwise queues a render. Requesting the same key twice queues once.
    void request(const QString &source, bool dark);

    /// Queue length including the render in flight; used by tests and the status bar.
    int pendingCount() const;

Q_SIGNALS:
    /// A diagram finished. `path` is the cache file.
    void rendered(const QString &key, const QString &path);
    void failed(const QString &key, const QString &error);
    /// The queue drained (every request either finished or failed).
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

    IMermaidRenderer *m_renderer = nullptr;   ///< Not owned
    QString m_cacheDir;
    QQueue<Job> m_queue;
    bool m_busy = false;
    Job m_current;
    QHash<QString, bool> m_queued;   ///< key -> queued, to avoid duplicates
};
