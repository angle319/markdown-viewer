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
        QStringLiteral("#8f2d56"),   // codeInline    6.67:1 on #f4eaee, 色相 335°
        QStringLiteral("#f4eaee"),   // codeInlineBackground
        QStringLiteral("#8c8c8c"),   // border       3.36:1（非文字）
        QStringLiteral("#e8e8e8"),   // tableHeader
    };
    static const Colors black{
        QStringLiteral("#000000"),   // background
        QStringLiteral("#ebebeb"),   // text        17.62:1
        QStringLiteral("#a6a6a6"),   // muted        8.63:1
        QStringLiteral("#7fb5ff"),   // link         9.96:1
        QStringLiteral("#131313"),   // codeBackground
        QStringLiteral("#f0a3c8"),   // codeInline    9.25:1 on #1d1418, 色相 331°
        QStringLiteral("#1d1418"),   // codeInlineBackground
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
    // font-*、margin、padding、text-decoration、以及表格的 background-color。
    // flex / grid / max-width / border-left 都不支援 —— 欄寬靠 viewport margin，
    // 標題分隔線與引用色條靠 MdTextBrowser::paintEvent 自繪。
    //
    // 刻意用具名 token 而不是 %1 %2：QString::arg 的多引數版本是按「字串中出現的
    // placeholder 由小到大」依序取代，不是 %N 對應第 N 個引數。少用掉一個編號
    // （例如 %4）就會讓其後全部錯位 —— 這個坑真的踩過，害 code 的底色拿到前景色。
    QString css = QStringLiteral(R"(
        body { color: @TEXT@; background-color: @BG@; }
        h1 { font-size: 23pt; margin-top: 24px; margin-bottom: 10px; color: @TEXT@; }
        h2 { font-size: 17pt; margin-top: 26px; margin-bottom: 10px; color: @TEXT@; }
        h3 { font-size: 14pt; margin-top: 22px; margin-bottom: 6px; color: @TEXT@; }
        h4, h5, h6 { font-size: 12pt; margin-top: 18px; margin-bottom: 6px; color: @TEXT@; }
        p  { margin-top: 7px; margin-bottom: 7px; }
        a  { color: @LINK@; text-decoration: underline; }
        code { color: @CODE_FG@; background-color: @CODE_BG@; font-family: monospace; }
        blockquote { color: @MUTED@; margin-left: @BQ@px; margin-top: 8px; margin-bottom: 8px; }
        th { background-color: @TH@; color: @TEXT@; }
        td { color: @TEXT@; }
        hr { color: @BORDER@; }
    )");

    const QList<QPair<QString, QString>> tokens{
        { QStringLiteral("@TEXT@"), c.text },
        { QStringLiteral("@BG@"), c.background },
        { QStringLiteral("@LINK@"), c.link },
        { QStringLiteral("@MUTED@"), c.muted },
        { QStringLiteral("@TH@"), c.tableHeader },
        { QStringLiteral("@BORDER@"), c.border },
        { QStringLiteral("@CODE_FG@"), c.codeInline },
        { QStringLiteral("@CODE_BG@"), c.codeInlineBackground },
        { QStringLiteral("@BQ@"), QString::number(BlockquoteIndentPx) },
    };
    for (const auto &t : tokens)
        css.replace(t.first, t.second);

    return css;
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
