#pragma once

#include "Theme.h"
#include "core/Document.h"

#include <QObject>
#include <QUrl>

class QWidget;

/// 顯示後端的抽象接縫。
///
/// 存在的唯一理由：日後若 QTextBrowser 的 CSS 子集不夠用，換成 litehtml
/// 只需新增一個實作，MainWindow / TocPanel / 解析層完全不必改。
class IRenderBackend : public QObject
{
    Q_OBJECT

public:
    explicit IRenderBackend(QObject *parent = nullptr);
    ~IRenderBackend() override;

    /// 實際要放進版面的 widget，所有權屬於後端。
    virtual QWidget *widget() = 0;

    virtual void setDocument(const Document &doc) = 0;
    virtual void setTheme(Theme::Mode mode) = 0;

    virtual void scrollToAnchor(const QString &anchor) = 0;
    virtual int scrollValue() const = 0;
    virtual void setScrollValue(int value) = 0;

    /// 某個 mermaid 圖的快取檔已就緒，請重新取用該資源並重畫。
    virtual void mermaidReady(const QString &key) = 0;

    virtual void zoomIn() = 0;
    virtual void zoomOut() = 0;
    virtual void resetZoom() = 0;

Q_SIGNALS:
    /// 使用者點了連結（含 #anchor、相對路徑、外部 URL），由 MainWindow 決定怎麼處理。
    void linkActivated(const QUrl &url);
    /// 視埠最上方的標題換了；index 為 Document::toc 的索引，-1 表示無。
    void currentTocIndexChanged(int index);
};
