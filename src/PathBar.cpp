#include "PathBar.h"

#include <QCompleter>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

PathBar::PathBar(QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
    , m_model(new QFileSystemModel(this))
    , m_open(new QToolButton(this))
{
    m_edit->setClearButtonEnabled(true);
    m_edit->setPlaceholderText(QStringLiteral("輸入檔案或資料夾路徑，Enter 開啟（Ctrl+L 聚焦）"));
    m_edit->installEventFilter(this);

    // Completion covers the whole file system with no name filter: the user may
    // well be navigating to a directory rather than a file
    m_model->setRootPath(QStringLiteral("/"));
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_edit->setCompleter(m_completer);

    m_open->setText(QStringLiteral("開啟"));
    m_open->setToolTip(QStringLiteral("開啟輸入的路徑"));
    connect(m_open, &QToolButton::clicked, this, &PathBar::submit);
    connect(m_edit, &QLineEdit::returnPressed, this, &PathBar::submit);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(6);
    layout->addWidget(new QLabel(QStringLiteral("路徑"), this));
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_open);
}

void PathBar::setPath(const QString &absolutePath)
{
    m_current = absolutePath;
    m_edit->setText(absolutePath);
    m_edit->setCursorPosition(absolutePath.size());
}

void PathBar::focusAndSelectAll()
{
    m_edit->setFocus(Qt::ShortcutFocusReason);
    m_edit->selectAll();
}

QString PathBar::resolveInput(const QString &input, const QString &baseDir)
{
    QString s = input.trimmed();
    if (s.isEmpty())
        return {};

    // Strip the noise that commonly arrives via paste
    if (s.startsWith(QLatin1String("file://")))
        s = QUrl(s).toLocalFile();

    if (s == QLatin1String("~"))
        return QDir::homePath();
    if (s.startsWith(QLatin1String("~/")))
        s = QDir::homePath() + s.mid(1);

    if (QDir::isAbsolutePath(s))
        return QDir::cleanPath(s);

    const QString base = baseDir.isEmpty() ? QDir::currentPath() : baseDir;
    return QDir::cleanPath(QDir(base).absoluteFilePath(s));
}

void PathBar::submit()
{
    const QString baseDir = m_current.isEmpty() ? QString()
                                                : QFileInfo(m_current).absolutePath();
    const QString resolved = resolveInput(m_edit->text(), baseDir);
    if (resolved.isEmpty())
        return;
    Q_EMIT pathSubmitted(resolved);
}

bool PathBar::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_edit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            m_edit->setText(m_current);
            Q_EMIT cancelled();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
