#include "PaneGroup.h"

#include "DocumentView.h"
#include "Theme.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QRubberBand>
#include <QFileInfo>
#include <QFrame>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

// ---------------------------------------------------------------- PaneTabBar

QSize PaneTabBar::tabSizeHint(int index) const
{
    QSize hint = QTabBar::tabSizeHint(index);
    hint.setWidth(qBound(MinTabWidth, hint.width(), MaxTabWidth));
    return hint;
}

void PaneTabBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_pressPos = e->pos();
        m_pressedTab = tabAt(e->pos());
        m_dragging = false;
    }
    QTabBar::mousePressEvent(e);
}

void PaneTabBar::mouseMoveEvent(QMouseEvent *e)
{
    // Only take over once the cursor leaves the bar *vertically*; otherwise let
    // QTabBar do its own within-bar reordering. Using vertical distance is
    // deliberate: horizontal movement means the user wants to reorder.
    const bool leftBarVertically =
        e->pos().y() < -8 || e->pos().y() > height() + 8;
    const bool farEnough =
        (e->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance();

    if (!m_dragging && m_pressedTab >= 0 && (e->buttons() & Qt::LeftButton)
        && leftBarVertically && farEnough) {
        m_dragging = true;

        Q_EMIT tabDragOut(m_pressedTab);

        auto *mime = new QMimeData;
        mime->setData(QString::fromLatin1(mimeType()), QByteArray("1"));

        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::MoveAction);
        m_pressedTab = -1;
        m_dragging = false;
        return;
    }
    QTabBar::mouseMoveEvent(e);
}

// ----------------------------------------------------------------- PaneGroup

PaneGroup::DropZone PaneGroup::zoneFor(const QRect &paneRect, const QPoint &pos)
{
    // The outer 25% on each side splits on that side; the middle merges in
    const int edge = qMax(40, paneRect.width() / 4);
    if (pos.x() < paneRect.left() + edge)
        return DropZone::SplitLeft;
    if (pos.x() > paneRect.right() - edge)
        return DropZone::SplitRight;
    return DropZone::Into;
}

PaneGroup::PaneGroup(QWidget *parent)
    : QWidget(parent)
    , m_tabBar(new PaneTabBar(this))
    , m_stack(new QStackedWidget(this))
    , m_activeLine(new QFrame(this))
{
    m_tabBar->setTabsClosable(true);
    m_tabBar->setMovable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setElideMode(Qt::ElideMiddle);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setDrawBase(false);

    m_activeLine->setFrameShape(QFrame::HLine);
    m_activeLine->setFixedHeight(2);
    m_activeLine->setVisible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabBar);
    layout->addWidget(m_activeLine);
    layout->addWidget(m_stack, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        Q_EMIT currentChanged();
    });
    connect(m_tabBar, &QTabBar::tabBarClicked, this, [this](int) { Q_EMIT activated(); });
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &PaneGroup::closeRequested);
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        // The tab bar's order is the single source of truth; move the stack too
        QWidget *w = m_stack->widget(from);
        m_stack->removeWidget(w);
        m_stack->insertWidget(to, w);
        m_stack->setCurrentIndex(m_tabBar->currentIndex());
        Q_EMIT tabsChanged();
    });

    connect(m_tabBar, &PaneTabBar::tabDragOut, this, &PaneGroup::tabDragOut);

    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabBar, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int index = m_tabBar->tabAt(pos);
        if (index < 0)
            return;
        Q_EMIT activated();
        Q_EMIT tabContextMenuRequested(index, m_tabBar->mapToGlobal(pos));
    });

    m_dropHint = new QRubberBand(QRubberBand::Rectangle, this);
    setAcceptDrops(true);
    // Narrower than this and tables and inline code wrap one character per line
    setMinimumWidth(240);
    installEventFilter(this);
}

void PaneGroup::showDropHint(DropZone zone)
{
    QRect r = rect();
    switch (zone) {
    case DropZone::SplitLeft:
        r.setWidth(qMax(40, r.width() / 4));
        break;
    case DropZone::SplitRight:
        r.setLeft(r.right() - qMax(40, r.width() / 4));
        break;
    case DropZone::Into:
        break;
    }
    m_dropHint->setGeometry(r);
    m_dropHint->show();
}

void PaneGroup::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(PaneTabBar::mimeType()))) {
        e->acceptProposedAction();
        showDropHint(zoneFor(rect(), e->position().toPoint()));
    } else {
        e->ignore();
    }
}

void PaneGroup::dragMoveEvent(QDragMoveEvent *e)
{
    if (!e->mimeData()->hasFormat(QString::fromLatin1(PaneTabBar::mimeType()))) {
        e->ignore();
        return;
    }
    e->acceptProposedAction();
    showDropHint(zoneFor(rect(), e->position().toPoint()));
}

void PaneGroup::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropHint->hide();
}

void PaneGroup::dropEvent(QDropEvent *e)
{
    m_dropHint->hide();
    if (!e->mimeData()->hasFormat(QString::fromLatin1(PaneTabBar::mimeType()))) {
        e->ignore();
        return;
    }
    e->acceptProposedAction();
    Q_EMIT tabDropped(this, zoneFor(rect(), e->position().toPoint()));
}

bool PaneGroup::eventFilter(QObject *obj, QEvent *event)
{
    // A press anywhere in the pane counts as using it
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn)
        Q_EMIT activated();
    return QWidget::eventFilter(obj, event);
}

int PaneGroup::count() const { return m_tabBar->count(); }

DocumentView *PaneGroup::viewAt(int index) const
{
    if (index < 0 || index >= m_stack->count())
        return nullptr;
    return qobject_cast<DocumentView *>(m_stack->widget(index));
}

DocumentView *PaneGroup::currentView() const { return viewAt(m_tabBar->currentIndex()); }
int PaneGroup::currentIndex() const { return m_tabBar->currentIndex(); }

void PaneGroup::setCurrentIndex(int index)
{
    if (index >= 0 && index < m_tabBar->count())
        m_tabBar->setCurrentIndex(index);
}

int PaneGroup::indexOf(DocumentView *view) const
{
    for (int i = 0; i < m_stack->count(); ++i)
        if (m_stack->widget(i) == view)
            return i;
    return -1;
}

int PaneGroup::indexOfPath(const QString &absolutePath) const
{
    for (int i = 0; i < m_stack->count(); ++i)
        if (DocumentView *v = viewAt(i); v && v->path() == absolutePath)
            return i;
    return -1;
}

QStringList PaneGroup::paths() const
{
    QStringList out;
    for (int i = 0; i < m_stack->count(); ++i)
        if (DocumentView *v = viewAt(i))
            out << v->path();
    return out;
}

int PaneGroup::addView(DocumentView *view)
{
    const int index = m_stack->addWidget(view);
    const QString title = view->title();
    m_tabBar->insertTab(index, title.isEmpty() ? QFileInfo(view->path()).fileName() : title);
    m_tabBar->setTabToolTip(index, view->path());
    view->installEventFilter(this);
    Q_EMIT tabsChanged();
    return index;
}

DocumentView *PaneGroup::takeView(int index)
{
    DocumentView *view = viewAt(index);
    if (!view)
        return nullptr;
    view->removeEventFilter(this);
    m_stack->removeWidget(view);
    m_tabBar->removeTab(index);
    Q_EMIT tabsChanged();
    return view;
}

void PaneGroup::removeView(int index)
{
    if (DocumentView *view = takeView(index)) {
        view->setParent(nullptr);
        view->deleteLater();
    }
}

void PaneGroup::refreshTabText(DocumentView *view)
{
    const int i = indexOf(view);
    if (i < 0)
        return;
    const QString title = view->title();
    m_tabBar->setTabText(i, title.isEmpty() ? QStringLiteral("(未命名)") : title);
    m_tabBar->setTabToolTip(i, view->path());
}

void PaneGroup::applyTheme(Theme::Mode mode)
{
    m_tabBar->setStyleSheet(Theme::tabBarStyleSheet(mode));
}

void PaneGroup::setActive(bool active, bool multiPane)
{
    m_active = active;

    QPalette pal = m_activeLine->palette();
    pal.setColor(QPalette::Window, palette().color(QPalette::Highlight));
    m_activeLine->setPalette(pal);
    m_activeLine->setAutoFillBackground(true);
    m_activeLine->setVisible(active && multiPane);
}

bool PaneGroup::isActiveIndicatorVisible() const
{
    return m_activeLine->isVisible();
}
