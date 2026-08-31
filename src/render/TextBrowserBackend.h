#pragma once

#include "render/IRenderBackend.h"

#include <QHash>
#include <QSize>
#include <QVector>

class MermaidCache;
class MdTextBrowser;

/// 以 QTextBrowser（QTextDocument 富文字引擎）顯示。無瀏覽器引擎、無 JS。
///
/// mermaid 圖以 <img src="mermaid://<sha1>"> 佔位，實際內容由覆寫的
/// loadResource() 從 MermaidCache 取；還沒畫好時先給一張「產生中」的佔位圖，
/// 畫好後由 mermaidReady() 換掉並重新排版。
class TextBrowserBackend : public IRenderBackend
{
    Q_OBJECT

public:
    /// @param cache 不取得所有權
    explicit TextBrowserBackend(MermaidCache *cache, QObject *parent = nullptr);
    ~TextBrowserBackend() override;

    QWidget *widget() override;

    void setDocument(const Document &doc) override;
    void setTheme(Theme::Mode mode) override;

    void scrollToAnchor(const QString &anchor) override;
    int scrollValue() const override;
    void setScrollValue(int value) override;

    void mermaidReady(const QString &key) override;

    void zoomIn() override;
    void zoomOut() override;
    void resetZoom() override;

private:
    void render(bool preserveScroll);
    void emitCurrentTocIndex();

    MdTextBrowser *m_view = nullptr;
    MermaidCache *m_cache = nullptr;
    Document m_doc;
    Theme::Mode m_mode = Theme::Light;
    int m_zoomSteps = 0;
    int m_lastTocIndex = -2;
};
