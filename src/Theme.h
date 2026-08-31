#pragma once

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
        QString border;
        // 註：引用區塊沒有左側色條 —— Qt rich-text 不支援 border-left，
        // 只能靠縮排表現，所以這裡刻意沒有 quoteBar 這種欄位。
        QString tableHeader;
    };

    static const Colors &colors(Mode m);
    static QString documentStyleSheet(Mode m);
    static QPalette palette(Mode m);

    static QString name(Mode m);

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
