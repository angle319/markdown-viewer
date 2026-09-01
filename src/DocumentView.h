#pragma once

#include "Theme.h"
#include "core/Document.h"

#include <QWidget>

class FileWatcher;
class IRenderBackend;
class MermaidCache;

/// The complete state and view of one markdown document.
///
/// This class exists so that several documents can be open at once: path,
/// source, Document, render backend and file watcher all used to hang off
/// MainWindow, which structurally allowed only one. With them extracted, a tab
/// is a DocumentView and a split pane simply shows a different one.
///
/// MermaidCache is shared from outside rather than owned per document: its key
/// is a content hash, so the same diagram in two documents should reuse one
/// cached image.
class DocumentView : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentView(MermaidCache *cache, QWidget *parent = nullptr);
    ~DocumentView() override;

    bool openFile(const QString &path);

    QString path() const { return m_path; }
    /// The document's first H1, falling back to the file name. Empty when no
    /// document is loaded.
    QString title() const;
    const Document &document() const { return m_doc; }
    bool isEmpty() const { return m_path.isEmpty(); }

    void setTheme(Theme::Mode mode);
    Theme::Mode theme() const { return m_mode; }

    void scrollToAnchor(const QString &anchor);
    int scrollValue() const;
    void setScrollValue(int value);

    void zoomIn();
    void zoomOut();
    void resetZoom();

    IRenderBackend *backend() const { return m_backend; }

Q_SIGNALS:
    /// The title changed (a different file was opened, or a reload changed the H1)
    void titleChanged();
    /// The document was replaced; the TOC needs rebuilding
    void documentReplaced();
    /// The heading at the top of the viewport changed; index is into Document::toc
    void currentTocIndexChanged(int index);
    /// A link was activated
    void linkActivated(const QUrl &url);
    /// A message for the status bar
    void statusMessage(const QString &text);

private:
    void reparse(bool preserveScroll);

    MermaidCache *m_cache = nullptr;       ///< Not owned
    IRenderBackend *m_backend = nullptr;
    FileWatcher *m_watcher = nullptr;

    QString m_path;
    QString m_markdown;
    Document m_doc;
    Theme::Mode m_mode = Theme::Light;
    bool m_degradeNoticeShown = false;
};

Q_DECLARE_METATYPE(DocumentView *)
