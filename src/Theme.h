#pragma once

#include "core/Typography.h"

#include <QColor>
#include <QPalette>
#include <QString>

/// 兩套主題：純白與純黑。
///
/// 列舉名沿用 Light/Dark（內部用語，牽動 mermaid 與語法高亮的 dark 旗標），
/// 但實際底色就是 #ffffff 與 #000000，UI 上顯示為「白色主題」「黑色主題」。
///
/// 對比是硬性要求，不是美感偏好：所有顏色組合都以 WCAG 2.1 相對亮度計算並由
/// tests/test_theme.cpp 驗證 —— 文字 ≥ 7:1、次要文字與連結 ≥ 4.5:1、
/// 框線等非文字元素 ≥ 3:1。改顏色若打破門檻，測試會失敗。
///
/// 內容欄寬上限沿用 Okular markdown generator 的 CONTENT_WIDTH（980px）。
class Theme
{
public:
    enum Mode { Light, Dark };

    static constexpr int ContentWidth = 980;

    /// 引用區塊的左邊距（px）。
    ///
    /// 這個常數是 CSS 與自繪程式碼之間的**契約**：Qt rich-text 不支援
    /// border-left，所以左側色條是在 paintEvent 裡自己畫的，而「哪些 block 是
    /// 引用區塊」就靠這個 leftMargin 值辨識（清單項目有 textList、標題的
    /// leftMargin 是 0，所以不會誤判）。
    /// 由 tests/test_e2e_regression.cpp 的 blockquoteMarkerContractHolds() 盯住。
    static constexpr int BlockquoteIndentPx = 22;

    /// 引用區塊左側色條寬度（px）
    static constexpr int BlockquoteBarPx = 4;

    /// WCAG 門檻
    static constexpr double MinTextContrast = 4.5;
    static constexpr double MinBodyTextContrast = 7.0;
    static constexpr double MinNonTextContrast = 3.0;

    struct Colors {
        QString background;
        QString text;
        QString muted;
        QString link;
        QString codeBackground;
        /// 行內 `code` 的專屬前景色。刻意選與連結色**色相差 > 110°**的洋紅系。
        ///
        /// 注意「可區分」不能用 WCAG 對比比來判斷 —— 那只衡量亮度差，紫色與藍色
        /// 的對比比只有 1.13:1 卻是明顯不同的顏色。這裡用色相差。
        /// 另外兩者的區分也不只靠顏色：連結有底線，行內 code 有底色 chip 與等寬字，
        /// 即使色覺不同的人也分得出來。
        QString codeInline;
        /// 行內 `code` 的底色（已把強調色以低透明度混進頁面底色，
        /// 因為 Qt rich-text 對 rgba() 的支援不可靠，改用預先算好的實色）。
        QString codeInlineBackground;
        QString border;
        // 註：引用區塊沒有左側色條 —— Qt rich-text 不支援 border-left，
        // 只能靠縮排表現，所以這裡刻意沒有 quoteBar 這種欄位。
        QString tableHeader;
    };

    static const Colors &colors(Mode m);
    static QString documentStyleSheet(Mode m);
    static QPalette palette(Mode m);

    static QString name(Mode m);

    /// 見 Typography —— 這兩個值 CodeHighlighter 也要用，所以定義在 core。
    static constexpr qreal BodyPointSize = Typography::BodyPointSize;
    static constexpr qreal LineHeightPercent = Typography::LineHeightPercent;

    /// 標題字級（pt），level 為 1..6。
    ///
    /// **刻意不透過 CSS 設定。** Qt 的 HTML 解析器對 h5/h6 會套用自己的
    /// fontSizeAdjustment，CSS 的 font-size 蓋不掉 —— 實測 H5 會變得比正文還小、
    /// H6 又比 H5 大，整個階層是壞的。所以字級由 render backend 在
    /// document tree walk 裡明確設定，這裡是唯一的定義處。
    static qreal headingPointSize(int level);

    // ---- 對比計算（WCAG 2.1）----

    /// 相對亮度，0（黑）到 1（白）。
    static double relativeLuminance(const QColor &c);

    /// 對比比，1.0（相同）到 21.0（純黑對純白）。
    static double contrastRatio(const QColor &a, const QColor &b);

    /// 回傳一個在 bg 上一定讀得到的文字色。
    /// 優先用該主題的正常文字色；若它在 bg 上對比不足，就退回純黑或純白中
    /// 對比較高的那個。用來救「markdown 內嵌原始 HTML 寫死顏色」這種情況。
    static QColor readableOn(const QColor &bg, Mode mode);
};
