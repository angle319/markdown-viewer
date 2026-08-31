#pragma once

#include <QWidget>

class QCompleter;
class QFileSystemModel;
class QLineEdit;
class QToolButton;

/// 可編輯的路徑列，行為比照瀏覽器的網址列。
///
/// * `Ctrl+L` 聚焦並全選（由 MainWindow 綁定）
/// * Enter 開啟輸入的路徑；是資料夾就切到側邊欄的「檔案」分頁並換根
/// * Esc 還原成目前檔案的路徑並把焦點交回內容區
/// * 支援 `~` 展開與相對路徑（相對於目前檔案所在目錄）
/// * 自動完成由 QFileSystemModel 提供
class PathBar : public QWidget
{
    Q_OBJECT

public:
    explicit PathBar(QWidget *parent = nullptr);

    /// 設定「目前檔案」的路徑；同時作為 Esc 還原與相對路徑解析的基準。
    void setPath(const QString &absolutePath);
    QString path() const { return m_current; }

    void focusAndSelectAll();

    /// 把使用者輸入轉成絕對路徑：展開 `~`、相對路徑對基準目錄解析。
    /// 純邏輯，方便單元測試。
    static QString resolveInput(const QString &input, const QString &baseDir);

Q_SIGNALS:
    /// 使用者按下 Enter，且輸入非空。path 已是絕對路徑。
    void pathSubmitted(const QString &path);
    /// 使用者按下 Esc。
    void cancelled();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void submit();

    QLineEdit *m_edit = nullptr;
    QFileSystemModel *m_model = nullptr;
    QCompleter *m_completer = nullptr;
    QToolButton *m_open = nullptr;
    QString m_current;
};
