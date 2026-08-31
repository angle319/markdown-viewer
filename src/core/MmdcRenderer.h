#pragma once

#include "core/IMermaidRenderer.h"

#include <QScopedPointer>

class QProcess;
class QTemporaryDir;

/// 用外部的 mermaid-cli（mmdc）算圖。
///
/// 實測（2026-08-31, mmdc 11.16.0）必須帶的兩個設定：
///   * puppeteer: --no-sandbox --disable-dev-shm-usage
///   * mermaid:   htmlLabels:false
///
/// htmlLabels 是關鍵：mermaid 預設把節點文字放進 <foreignObject> 裡的 HTML，
/// 而 Qt 的 QSvgRenderer 只支援 SVG Tiny 1.2、不認 foreignObject，
/// 結果是圖形畫得出來但**所有文字都是空白**。關掉後會產出真正的 <text> 元素。
///
/// **預設輸出是 PNG，不是 SVG。** 即使關掉 htmlLabels，Qt SVG Tiny 仍然
/// 不支援 <marker>，實測結果是所有連線與箭頭整批消失、節點文字被畫到方框
/// 上緣、邊標籤出現灰色方塊、原點還留一個黑色三角形。改由 mmdc（Chromium）
/// 自己光柵化成 PNG 就完全正確。SVG 模式保留為可切換，但已知與 Qt 不相容。
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

    /// "png"（預設，正確）或 "svg"（解析度無關，但 Qt 畫不對，見類別註解）。
    void setOutputExtension(const QString &ext) { m_outputExtension = ext; }

    /// PNG 的光柵化倍率，預設 2（HiDPI 下不糊）。
    void setPngScale(int s) { m_pngScale = qMax(1, s); }

    /// 找不到 mmdc 時回傳空字串。會找 PATH，也會找 nvm 的安裝位置
    /// （從桌面啟動器啟動時 PATH 通常不含 ~/.nvm/...）。
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
