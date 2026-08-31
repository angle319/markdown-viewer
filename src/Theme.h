#pragma once

#include <QPalette>
#include <QString>

/// 明暗兩套配色。CSS 以 QTextDocument::setDefaultStyleSheet() 注入。
///
/// 內容欄寬上限沿用 Okular markdown generator 的 CONTENT_WIDTH（980px）：
/// 超寬圖片等比縮到欄內，同時讓長行不會拉滿整個寬螢幕。
class Theme
{
public:
    enum Mode { Light, Dark };

    static constexpr int ContentWidth = 980;

    struct Colors {
        QString background;
        QString text;
        QString muted;
        QString link;
        QString codeBackground;
        QString border;
        // 註：引用區塊沒有左側色條 —— Qt rich-text 不支援 border-left，
        // 只能靠縮排表現，所以這裡刻意沒有 quoteBar 這種欄位。
        QString tableHeader;
    };

    static const Colors &colors(Mode m);
    static QString documentStyleSheet(Mode m);
    static QPalette palette(Mode m);
};
