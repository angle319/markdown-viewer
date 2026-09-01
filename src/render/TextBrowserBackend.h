#pragma once

#include "render/IRenderBackend.h"

#include <QHash>
#include <QSize>
#include <QVector>

class MermaidCache;
class MdTextBrowser;

/// Displays a document with QTextBrowser (Qt's QTextDocument rich-text engine).
/// No browser engine, no JavaScript.
///
/// Mermaid diagrams are placeholders of the form <img src="mermaid://<sha1>">.
/// The overridden loadResource() fetches the real image from MermaidCache, or
/// returns a "rendering" placeholder while it is still being produced;
/// mermaidReady() then swaps it in and re-lays out.
class TextBrowserBackend : public IRenderBackend
{
    Q_OBJECT

public:
    /// @param cache Not owned
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

    /// Zoom factor. 1.0 is the original size.
    void setZoom(qreal factor);
    qreal zoom() const { return m_zoom; }

    static constexpr qreal ZoomStep = 1.1;
    static constexpr qreal MinZoom = 0.5;
    static constexpr qreal MaxZoom = 3.0;

private:
    void render(bool preserveScroll);
    void emitCurrentTocIndex();

    MdTextBrowser *m_view = nullptr;
    MermaidCache *m_cache = nullptr;
    Document m_doc;
    Theme::Mode m_mode = Theme::Light;
    qreal m_zoom = 1.0;
    int m_lastTocIndex = -2;
};
