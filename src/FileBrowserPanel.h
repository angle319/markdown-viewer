#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QTreeView;

/// The sidebar's Files tab: a tree showing only directories and markdown-like
/// files.
class FileBrowserPanel : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowserPanel(QWidget *parent = nullptr);

    void setRoot(const QString &dir);
    QString root() const { return m_root; }

    /// Selects and scrolls to a file, when it is under the current root.
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
