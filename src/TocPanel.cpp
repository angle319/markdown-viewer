#include "TocPanel.h"

#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

TocPanel::TocPanel(QWidget *parent)
    : QWidget(parent)
    , m_tree(new QTreeWidget(this))
{
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        if (m_suppress || !item)
            return;
        const QString anchor = item->data(0, Qt::UserRole + 1).toString();
        if (!anchor.isEmpty())
            Q_EMIT anchorActivated(anchor);
    });
}

void TocPanel::setToc(const QVector<TocEntry> &toc)
{
    m_tree->clear();
    m_items.clear();
    m_items.resize(toc.size());

    // Rebuild the nesting with a (level, item) stack. Heading levels may skip
    // (an H1 followed directly by an H3), so this looks for the first ancestor
    // with a smaller level rather than assuming a one-level step.
    QVector<QPair<int, QTreeWidgetItem *>> stack;

    for (int i = 0; i < toc.size(); ++i) {
        const TocEntry &e = toc.at(i);

        while (!stack.isEmpty() && stack.last().first >= e.level)
            stack.removeLast();

        auto *item = stack.isEmpty() ? new QTreeWidgetItem(m_tree)
                                     : new QTreeWidgetItem(stack.last().second);
        item->setText(0, e.text.isEmpty() ? QStringLiteral("(無標題)") : e.text);
        item->setData(0, Qt::UserRole, i);
        item->setData(0, Qt::UserRole + 1, e.anchor);
        item->setToolTip(0, QStringLiteral("H%1  #%2").arg(e.level).arg(e.anchor));

        m_items[i] = item;
        stack.append({ e.level, item });
    }

    m_tree->expandAll();
}

void TocPanel::highlightIndex(int index)
{
    if (index < 0 || index >= m_items.size() || !m_items.at(index)) {
        m_suppress = true;
        m_tree->clearSelection();
        m_suppress = false;
        return;
    }

    m_suppress = true;
    m_tree->setCurrentItem(m_items.at(index));
    m_tree->scrollToItem(m_items.at(index), QAbstractItemView::EnsureVisible);
    m_suppress = false;
}
