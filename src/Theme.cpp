#include "Theme.h"

#include <cmath>

namespace {

double channel(double srgb)
{
    return srgb <= 0.03928 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}

} // namespace

const Theme::Colors &Theme::colors(Mode m)
{
    // 這些數值是算過對比的，別隨手改；門檻由 tests/test_theme.cpp 守著。
    static const Colors white{
        QStringLiteral("#ffffff"),   // background
        QStringLiteral("#111111"),   // text        18.88:1
        QStringLiteral("#454545"),   // muted        9.59:1
        QStringLiteral("#0b57d0"),   // link         6.39:1
        QStringLiteral("#f2f2f2"),   // codeBackground
        QStringLiteral("#8c8c8c"),   // border       3.36:1（非文字）
        QStringLiteral("#e8e8e8"),   // tableHeader
    };
    static const Colors black{
        QStringLiteral("#000000"),   // background
        QStringLiteral("#ebebeb"),   // text        17.62:1
        QStringLiteral("#a6a6a6"),   // muted        8.63:1
        QStringLiteral("#7fb5ff"),   // link         9.96:1
        QStringLiteral("#131313"),   // codeBackground
        QStringLiteral("#707070"),   // border       4.24:1（非文字）
        QStringLiteral("#1c1c1c"),   // tableHeader
    };
    return m == Dark ? black : white;
}

QString Theme::name(Mode m)
{
    return m == Dark ? QStringLiteral("黑色主題") : QStringLiteral("白色主題");
}

QString Theme::documentStyleSheet(Mode m)
{
    const Colors &c = colors(m);

    // 只用 Qt rich-text 支援的 CSS 屬性子集：color、background-color、
    // font-*、margin、padding、border（表格）、text-decoration。
    // flex / grid / max-width 都不支援，欄寬靠 viewport margin 處理。
    return QStringLiteral(R"(
        body { color: %1; background-color: %2; }
        h1 { font-size: 24pt; margin-top: 18px; margin-bottom: 10px; color: %1; }
        h2 { font-size: 19pt; margin-top: 16px; margin-bottom: 8px; color: %1; }
        h3 { font-size: 16pt; margin-top: 14px; margin-bottom: 6px; color: %1; }
        h4, h5, h6 { font-size: 13pt; margin-top: 12px; margin-bottom: 6px; color: %1; }
        p  { margin-top: 6px; margin-bottom: 6px; }
        a  { color: %3; text-decoration: none; }
        code { background-color: %4; color: %1; font-family: monospace; }
        blockquote { color: %5; margin-left: 12px; padding-left: 10px; }
        th { background-color: %6; color: %1; }
        td { color: %1; }
        hr { color: %7; }
    )")
        .arg(c.text, c.background, c.link, c.codeBackground, c.muted, c.tableHeader, c.border);
}

QPalette Theme::palette(Mode m)
{
    // 必須把 role 設滿。只設 Window/Base/Text 的話，QTabBar 與 QMenuBar 會用
    // 預設的 Button/ButtonText（淺色系）去畫，在黑色主題下就變成
    // 「淺色底 + 淺色字」的隱形分頁標籤與隱形選單 —— 實際踩過。
    const Colors &c = colors(m);
    const QColor bg(c.background);
    const QColor text(c.text);
    const QColor muted(c.muted);
    const QColor link(c.link);
    const QColor chrome(c.tableHeader);      // 按鈕／分頁的底色
    const QColor edge(c.border);

    QPalette p;

    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, QColor(c.codeBackground));
    p.setColor(QPalette::Text, text);

    p.setColor(QPalette::Button, chrome);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, readableOn(text, m));

    p.setColor(QPalette::ToolTipBase, chrome);
    p.setColor(QPalette::ToolTipText, text);

    p.setColor(QPalette::Link, link);
    p.setColor(QPalette::LinkVisited, link);
    p.setColor(QPalette::Highlight, link);
    p.setColor(QPalette::HighlightedText, readableOn(link, m));
    p.setColor(QPalette::PlaceholderText, muted);

    // 立體邊框用的一組灰階
    p.setColor(QPalette::Light, m == Dark ? QColor(0x2a, 0x2a, 0x2a) : QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::Midlight, m == Dark ? QColor(0x22, 0x22, 0x22) : QColor(0xf2, 0xf2, 0xf2));
    p.setColor(QPalette::Mid, edge);
    p.setColor(QPalette::Dark, edge);
    p.setColor(QPalette::Shadow, m == Dark ? QColor(0x00, 0x00, 0x00) : QColor(0x8c, 0x8c, 0x8c));

    // 停用狀態仍需看得見（WCAG 對非文字元素的 3:1）
    p.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    p.setColor(QPalette::Disabled, QPalette::Text, muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, muted);

    return p;
}

double Theme::relativeLuminance(const QColor &c)
{
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF())
           + 0.0722 * channel(c.blueF());
}

double Theme::contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = qMax(la, lb);
    const double lo = qMin(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

QColor Theme::readableOn(const QColor &bg, Mode mode)
{
    const QColor themeText(colors(mode).text);
    if (contrastRatio(themeText, bg) >= MinTextContrast)
        return themeText;

    const QColor black(Qt::black);
    const QColor white(Qt::white);
    return contrastRatio(black, bg) >= contrastRatio(white, bg) ? black : white;
}
