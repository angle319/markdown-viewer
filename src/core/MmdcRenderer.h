#pragma once

#include "core/IMermaidRenderer.h"

#include <QScopedPointer>

class QProcess;
class QTemporaryDir;

/// Renders diagrams with the external mermaid-cli (mmdc).
///
/// Two settings are required, established by measurement (2026-08-31, mmdc
/// 11.16.0):
///   * puppeteer: --no-sandbox --disable-dev-shm-usage
///   * mermaid:   htmlLabels:false
///
/// htmlLabels is the critical one: by default mermaid puts node text inside a
/// <foreignObject>, which Qt's QSvgRenderer (SVG Tiny 1.2) does not support at
/// all, so the shapes render but every label is blank. Disabling it emits real
/// <text> elements.
///
/// **The default output is PNG, not SVG.** Even with htmlLabels off, SVG Tiny
/// has no <marker>: every connector and arrowhead disappears, node labels land
/// at the top edge of their boxes, grey blocks appear beside edge labels, and a
/// stray black triangle is left at the origin. Letting mmdc (Chromium)
/// rasterise to PNG is correct. SVG remains selectable but is known to be
/// incompatible with Qt.
///
/// 中：SVG 經 Qt 會掉光連線與箭頭，所以預設輸出 PNG。
class MmdcRenderer : public IMermaidRenderer
{
    Q_OBJECT

public:
    explicit MmdcRenderer(QObject *parent = nullptr);
    ~MmdcRenderer() override;

    bool isAvailable() const override;
    QString rendererId() const override;
    QString outputExtension() const override { return m_outputExtension; }
    void start(const QString &source, bool dark, const QString &outPath) override;

    qreal outputScale() const override;

    /// "png" (default, correct) or "svg" (resolution independent, but Qt draws
    /// it wrong — see the class comment).
    void setOutputExtension(const QString &ext) { m_outputExtension = ext; }

    /// PNG rasterisation scale; follows the display so HiDPI stays sharp.
    void setPngScale(int s) { m_pngScale = qMax(1, s); }

    /// Returns an empty string when mmdc cannot be found. Searches PATH and the
    /// nvm install locations, because a desktop launcher's PATH usually does not
    /// include ~/.nvm/...
    static QString findMmdc();

private:
    QString m_exe;
    QString m_outputExtension = QStringLiteral("png");
    int m_pngScale = 2;
    mutable QString m_versionCache;
    QProcess *m_proc = nullptr;
    QScopedPointer<QTemporaryDir> m_workDir;
    QString m_outPath;
};
