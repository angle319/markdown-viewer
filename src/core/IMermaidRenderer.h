#pragma once

#include <QObject>
#include <QString>

/// 把 mermaid 原始碼算成一個圖檔。
///
/// 非同步介面：QProcess 本身就是事件驅動的，不需要額外的執行緒。
/// 實作可以是外部行程（MmdcRenderer），未來也可以是 graphviz 譯法。
class IMermaidRenderer : public QObject
{
    Q_OBJECT

public:
    explicit IMermaidRenderer(QObject *parent = nullptr) : QObject(parent) {}
    ~IMermaidRenderer() override;

    /// 外部工具是否就位。不可用時 MermaidCache 走 degrade 路徑（顯示原始碼）。
    virtual bool isAvailable() const = 0;

    /// 會影響輸出的識別字串，計入快取 key；工具升版後舊圖自然失效。
    virtual QString rendererId() const = 0;

    /// 產出的副檔名，"svg" 或 "png"。
    virtual QString outputExtension() const = 0;

    /// 啟動一次渲染。同一時間只會有一次進行中的渲染（由 MermaidCache 保證）。
    virtual void start(const QString &source, bool dark, const QString &outPath) = 0;

Q_SIGNALS:
    /// ok 為 false 時 error 說明原因。
    void finished(bool ok, const QString &error);
};
