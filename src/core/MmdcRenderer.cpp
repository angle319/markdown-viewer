#include "core/MmdcRenderer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

constexpr int kRenderTimeoutMs = 30000;

/// Mermaid config: htmlLabels must be off or Qt cannot draw the labels.
const char *kMermaidConfig =
    R"({"htmlLabels":false,"flowchart":{"htmlLabels":false},"class":{"htmlLabels":false}})";

/// Puppeteer config: the sandbox fails outright in containers and restricted
/// environments.
const char *kPuppeteerConfig =
    R"({"args":["--no-sandbox","--disable-dev-shm-usage"]})";

bool writeTextFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

/// mmdc's SVG root carries width="100%", which robs QSvgRenderer of an
/// intrinsic size. Strip it so the viewBox decides.
void stripPercentageWidth(const QString &svgPath)
{
    QFile f(svgPath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QByteArray data = f.readAll();
    f.close();

    const int svgTagEnd = data.indexOf('>');
    if (svgTagEnd < 0)
        return;

    QByteArray head = data.left(svgTagEnd);
    const QByteArray needle = QByteArrayLiteral(" width=\"100%\"");
    if (!head.contains(needle))
        return;

    head.replace(needle, QByteArray());
    data.replace(0, svgTagEnd, head);

    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(data);
}

} // namespace

MmdcRenderer::MmdcRenderer(QObject *parent)
    : IMermaidRenderer(parent)
    , m_exe(findMmdc())
{
}

MmdcRenderer::~MmdcRenderer()
{
    // If a render is still running at shutdown, destroying the QProcess emits
    // "Destroyed while process is still running" and may orphan the child.
    // Terminate it explicitly and reap it.
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(3000);
    }
}

QString MmdcRenderer::findMmdc()
{
    // An explicit override wins
    const QString override =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("MARKDOWN_TOOL_MMDC"));
    if (!override.isEmpty() && QFileInfo(override).isExecutable())
        return override;

    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("mmdc"));
    if (!onPath.isEmpty())
        return onPath;

    // A desktop launcher's PATH usually lacks nvm, so search for it
    const QString nvmRoot = QDir::homePath() + QStringLiteral("/.nvm/versions/node");
    const QFileInfoList versions =
        QDir(nvmRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo &v : versions) {
        const QString candidate = v.absoluteFilePath() + QStringLiteral("/bin/mmdc");
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    }
    return QString();
}

qreal MmdcRenderer::outputScale() const
{
    return m_outputExtension == QStringLiteral("png") ? qreal(m_pngScale) : 1.0;
}

bool MmdcRenderer::isAvailable() const
{
    return !m_exe.isEmpty();
}

QString MmdcRenderer::rendererId() const
{
    if (m_versionCache.isEmpty()) {
        if (m_exe.isEmpty()) {
            m_versionCache = QStringLiteral("mmdc-absent");
        } else {
            QProcess p;
            p.start(m_exe, { QStringLiteral("--version") });
            if (p.waitForFinished(10000) && p.exitCode() == 0) {
                const QString v = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
                m_versionCache =
                    QStringLiteral("mmdc-") + (v.isEmpty() ? QStringLiteral("unknown") : v);
            } else {
                m_versionCache = QStringLiteral("mmdc-unknown");
            }
        }
    }

    // The scale must be part of the id, and therefore of the cache key: output
    // pixels differ per scale, so sharing one cache file would hand back an
    // image at the wrong resolution after moving to another display.
    if (m_outputExtension == QStringLiteral("png"))
        return QStringLiteral("%1@%2x").arg(m_versionCache).arg(m_pngScale);
    return m_versionCache;
}

void MmdcRenderer::start(const QString &source, bool dark, const QString &outPath)
{
    if (m_exe.isEmpty()) {
        Q_EMIT finished(false, QStringLiteral("找不到 mmdc"));
        return;
    }

    m_workDir.reset(new QTemporaryDir);
    if (!m_workDir->isValid()) {
        Q_EMIT finished(false, QStringLiteral("無法建立暫存目錄"));
        return;
    }
    m_outPath = outPath;

    const QString dir = m_workDir->path();
    const QString inFile = dir + QStringLiteral("/diagram.mmd");
    const QString mConf = dir + QStringLiteral("/mermaid.json");
    const QString pConf = dir + QStringLiteral("/puppeteer.json");

    if (!writeTextFile(inFile, source.toUtf8())
        || !writeTextFile(mConf, QByteArray(kMermaidConfig))
        || !writeTextFile(pConf, QByteArray(kPuppeteerConfig))) {
        Q_EMIT finished(false, QStringLiteral("無法寫入暫存檔"));
        return;
    }

    const QStringList args{
        QStringLiteral("-i"), inFile,
        QStringLiteral("-o"), outPath,
        QStringLiteral("-c"), mConf,
        QStringLiteral("-p"), pConf,
        QStringLiteral("-b"), QStringLiteral("transparent"),
        QStringLiteral("-t"), dark ? QStringLiteral("dark") : QStringLiteral("default"),
    };
    QStringList fullArgs = args;
    if (m_outputExtension == QStringLiteral("png"))
        fullArgs << QStringLiteral("-s") << QString::number(m_pngScale);

    if (!m_proc) {
        m_proc = new QProcess(this);
        connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            const QString stderrText = QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
            const bool ok = (status == QProcess::NormalExit) && code == 0
                            && QFileInfo(m_outPath).size() > 0;
            if (ok && m_outputExtension == QStringLiteral("svg"))
                stripPercentageWidth(m_outPath);

            m_workDir.reset();
            Q_EMIT finished(ok, ok ? QString()
                                   : QStringLiteral("mmdc 失敗 (exit %1): %2")
                                         .arg(code).arg(stderrText.left(400)));
        });
        connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            if (m_proc->state() == QProcess::NotRunning) {
                m_workDir.reset();
                Q_EMIT finished(false, QStringLiteral("mmdc 無法啟動: ") + m_proc->errorString());
            }
        });
    }

    m_proc->start(m_exe, fullArgs);
    if (!m_proc->waitForStarted(kRenderTimeoutMs)) {
        m_workDir.reset();
        Q_EMIT finished(false, QStringLiteral("mmdc 啟動逾時"));
    }
}
