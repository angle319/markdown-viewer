#include "core/MermaidCache.h"
#include "core/IMermaidRenderer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

MermaidCache::MermaidCache(IMermaidRenderer *renderer, QObject *parent)
    : QObject(parent)
    , m_renderer(renderer)
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                 + QStringLiteral("/markdown-tool/mermaid");

    if (m_renderer)
        connect(m_renderer, &IMermaidRenderer::finished,
                this, &MermaidCache::onRendererFinished);
}

void MermaidCache::setCacheDir(const QString &dir)
{
    m_cacheDir = dir;
}

QString MermaidCache::keyFor(const QString &source, bool dark) const
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(source.toUtf8());
    h.addData(dark ? QByteArrayLiteral("|dark") : QByteArrayLiteral("|light"));
    h.addData(QStringLiteral("|%1")
                  .arg(m_renderer ? m_renderer->rendererId() : QStringLiteral("none"))
                  .toUtf8());
    return QString::fromLatin1(h.result().toHex());
}

QString MermaidCache::pathFor(const QString &source, bool dark) const
{
    const QString ext = m_renderer ? m_renderer->outputExtension() : QStringLiteral("svg");
    return QStringLiteral("%1/%2.%3").arg(m_cacheDir, keyFor(source, dark), ext);
}

bool MermaidCache::isCached(const QString &source, bool dark) const
{
    const QFileInfo fi(pathFor(source, dark));
    return fi.exists() && fi.size() > 0;
}

bool MermaidCache::rendererAvailable() const
{
    return m_renderer && m_renderer->isAvailable();
}

qreal MermaidCache::outputScale() const
{
    return m_renderer ? m_renderer->outputScale() : 1.0;
}

int MermaidCache::pendingCount() const
{
    return m_queue.size() + (m_busy ? 1 : 0);
}

void MermaidCache::request(const QString &source, bool dark)
{
    if (!rendererAvailable())
        return;
    if (isCached(source, dark))
        return;

    const QString key = keyFor(source, dark);
    if (m_queued.contains(key))
        return;

    Job j;
    j.key = key;
    j.source = source;
    j.dark = dark;
    j.outPath = pathFor(source, dark);

    m_queued.insert(key, true);
    m_queue.enqueue(j);
    startNext();
}

void MermaidCache::startNext()
{
    if (m_busy || m_queue.isEmpty())
        return;

    m_current = m_queue.dequeue();

    if (!QDir().mkpath(QFileInfo(m_current.outPath).absolutePath())) {
        m_queued.remove(m_current.key);
        Q_EMIT failed(m_current.key, QStringLiteral("無法建立快取目錄"));
        startNext();
        return;
    }

    m_busy = true;
    m_renderer->start(m_current.source, m_current.dark, m_current.outPath);
}

void MermaidCache::onRendererFinished(bool ok, const QString &error)
{
    const Job done = m_current;
    m_busy = false;
    m_queued.remove(done.key);

    if (ok && QFileInfo(done.outPath).size() > 0) {
        Q_EMIT rendered(done.key, done.outPath);
    } else {
        // Never leave a partial file in the cache; the next request would
        // mistake it for a hit
        QFile::remove(done.outPath);
        Q_EMIT failed(done.key, error.isEmpty() ? QStringLiteral("渲染失敗") : error);
    }

    if (m_queue.isEmpty())
        Q_EMIT idle();
    else
        startNext();
}
