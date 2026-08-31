#pragma once

#include "core/Document.h"

#include <QVector>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

/// 側邊欄「段落」分頁：標題樹，可點擊跳轉，並隨捲動高亮目前位置。
class TocPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TocPanel(QWidget *parent = nullptr);

    void setToc(const QVector<TocEntry> &toc);
    /// 由後端的捲動同步呼叫；index 為 Document::toc 索引，-1 表示清除高亮。
    void highlightIndex(int index);

Q_SIGNALS:
    void anchorActivated(const QString &anchor);

private:
    QTreeWidget *m_tree = nullptr;
    QVector<QTreeWidgetItem *> m_items;   ///< toc 索引 → item
    bool m_suppress = false;
};
