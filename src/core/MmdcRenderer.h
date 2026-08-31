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

    /// 切成 "png" 可完全避開 Qt SVG Tiny 的相容性風險（代價是不再解析度無關）。
    void setOutputExtension(const QString &ext) { m_outputExtension = ext; }

    /// 找不到 mmdc 時回傳空字串。會找 PATH，也會找 nvm 的安裝位置
    /// （從桌面啟動器啟動時 PATH 通常不含 ~/.nvm/...）。
    static QString findMmdc();

private:
    QString m_exe;
    QString m_outputExtension = QStringLiteral("svg");
    mutable QString m_versionCache;
    QProcess *m_proc = nullptr;
    QScopedPointer<QTemporaryDir> m_workDir;
    QString m_outPath;
};
