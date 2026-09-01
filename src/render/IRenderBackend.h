#pragma once

#include "Theme.h"
#include "core/Document.h"

#include <QObject>
#include <QUrl>

class QWidget;

/// The seam between the document model and whatever draws it.
///
/// It exists for one reason: if Qt's CSS subset ever proves insufficient, a
/// litehtml backend can be added as another implementation without touching
/// MainWindow, TocPanel or the parser.
class IRenderBackend : public QObject
{
    Q_OBJECT

public:
    explicit IRenderBackend(QObject *parent = nullptr);
    ~IRenderBackend() override;

    /// The widget to place in a layout. Owned by the backend.
    virtual QWidget *widget() = 0;

    virtual void setDocument(const Document &doc) = 0;
    virtual void setTheme(Theme::Mode mode) = 0;

    virtual void scrollToAnchor(const QString &anchor) = 0;
    virtual int scrollValue() const = 0;
    virtual void setScrollValue(int value) = 0;

    /// A mermaid diagram's cache file is ready; re-fetch the resource and redraw.
    virtual void mermaidReady(const QString &key) = 0;

    virtual void zoomIn() = 0;
    virtual void zoomOut() = 0;
    virtual void resetZoom() = 0;

Q_SIGNALS:
    /// A link was activated (#anchor, relative path or external URL).
    /// MainWindow decides what to do with it.
    void linkActivated(const QUrl &url);
    /// The heading at the top of the viewport changed. `index` is into
    /// Document::toc; -1 means none.
    void currentTocIndexChanged(int index);
};
