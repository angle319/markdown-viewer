#pragma once

#include "core/Document.h"

#include <QVector>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

/// The sidebar's Paragraphs tab: a heading tree that scrolls the document on
/// click and highlights the heading currently at the top of the viewport.
class TocPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TocPanel(QWidget *parent = nullptr);

    void setToc(const QVector<TocEntry> &toc);
    /// Called as the backend scrolls. `index` is into Document::toc; -1 clears.
    void highlightIndex(int index);

Q_SIGNALS:
    void anchorActivated(const QString &anchor);

private:
    QTreeWidget *m_tree = nullptr;
    QVector<QTreeWidgetItem *> m_items;   ///< toc index -> item
    bool m_suppress = false;
};
