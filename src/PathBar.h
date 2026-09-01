#pragma once

#include <QWidget>

class QCompleter;
class QFileSystemModel;
class QLineEdit;
class QToolButton;

/// An editable path bar that behaves like a browser's address bar.
///
/// * `Ctrl+L` focuses it and selects all (bound by MainWindow)
/// * Enter opens the path; a directory re-roots the sidebar's Files tab
/// * Escape restores the current path and returns focus to the document
/// * Accepts `~` and paths relative to the current document's directory
/// * Completion comes from QFileSystemModel
class PathBar : public QWidget
{
    Q_OBJECT

public:
    explicit PathBar(QWidget *parent = nullptr);

    /// Sets the current document's path. Also the base for Escape restore and
    /// for resolving relative input.
    void setPath(const QString &absolutePath);
    QString path() const { return m_current; }

    void focusAndSelectAll();

    /// Turns user input into an absolute path: expands `~` and resolves
    /// relative paths against the base directory. Pure logic, so it is unit
    /// testable on its own.
    static QString resolveInput(const QString &input, const QString &baseDir);

Q_SIGNALS:
    /// Enter was pressed with non-empty input. `path` is already absolute.
    void pathSubmitted(const QString &path);
    /// Escape was pressed.
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
