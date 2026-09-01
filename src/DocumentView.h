#pragma once

#include "Theme.h"
#include "core/Document.h"

#include <QWidget>

class FileWatcher;
class IRenderBackend;
class MermaidCache;

/// 一份 markdown 文件的完整狀態與畫面。
///
/// 這個類別存在的理由是多文件：原本 path / 原始碼 / Document / render backend /
/// 檔案監看全都掛在 MainWindow 上，只能開一份。抽出來之後，分頁就是一組
/// DocumentView，比較模式就是同時顯示其中幾個。
///
/// MermaidCache 由外部共用而非每份文件各持一份 —— 它的 key 是內容雜湊，
/// 不同文件裡相同的圖表本來就該共用同一張快取。
class DocumentView : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentView(MermaidCache *cache, QWidget *parent = nullptr);
    ~DocumentView() override;

    bool openFile(const QString &path);

    QString path() const { return m_path; }
    /// 文件的第一個 H1；沒有就用檔名。空文件回傳空字串。
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
    /// 標題變了（開了不同檔案、或檔案內容改變導致 H1 變了）
    void titleChanged();
    /// 文件被換掉，TOC 需要重建
    void documentReplaced();
    /// 視埠最上方的標題換了，index 為 Document::toc 的索引
    void currentTocIndexChanged(int index);
    /// 使用者點了連結
    void linkActivated(const QUrl &url);
    /// 想在狀態列顯示的訊息
    void statusMessage(const QString &text);

private:
    void reparse(bool preserveScroll);

    MermaidCache *m_cache = nullptr;       ///< 不擁有
    IRenderBackend *m_backend = nullptr;
    FileWatcher *m_watcher = nullptr;

    QString m_path;
    QString m_markdown;
    Document m_doc;
    Theme::Mode m_mode = Theme::Light;
    bool m_degradeNoticeShown = false;
};

Q_DECLARE_METATYPE(DocumentView *)
