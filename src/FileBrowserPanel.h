#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QTreeView;

/// 側邊欄「檔案」分頁：只顯示資料夾與 markdown 類檔案的檔案樹。
class FileBrowserPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowserPanel(QWidget *parent = nullptr);

    void setRoot(const QString &dir);
    QString root() const { return m_root; }

    /// 在樹中選取並捲到該檔案（若在目前根目錄底下）。
    void selectFile(const QString &path);

Q_SIGNALS:
    void fileActivated(const QString &path);

private Q_SLOTS:
    void chooseFolder();
    void goUp();

private:
    QFileSystemModel *m_model = nullptr;
    QTreeView *m_view = nullptr;
    QLabel *m_rootLabel = nullptr;
    QString m_root;
};
