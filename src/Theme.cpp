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
    // These values have been measured for contrast. Do not change them
    // casually; the thresholds are enforced by tests/test_theme.cpp.
    static const Colors white{
        QStringLiteral("#ffffff"),   // background
        QStringLiteral("#111111"),   // text        18.88:1
        QStringLiteral("#454545"),   // muted        9.59:1
        QStringLiteral("#0b57d0"),   // link         6.39:1
        QStringLiteral("#f2f2f2"),   // codeBackground
        QStringLiteral("#8f2d56"),   // codeInline    6.67:1 on #f4eaee, hue 335
        QStringLiteral("#f4eaee"),   // codeInlineBackground
        QStringLiteral("#e2e2e2"),   // tabInactive (unselected tab; muted text 7.40:1)
        QStringLiteral("#8c8c8c"),   // border       3.36:1 (non-text)
        QStringLiteral("#e8e8e8"),   // tableHeader
    };
    static const Colors black{
        QStringLiteral("#000000"),   // background
        QStringLiteral("#ebebeb"),   // text        17.62:1
        QStringLiteral("#a6a6a6"),   // muted        8.63:1
        QStringLiteral("#7fb5ff"),   // link         9.96:1
        QStringLiteral("#131313"),   // codeBackground
        QStringLiteral("#f0a3c8"),   // codeInline    9.25:1 on #1d1418, hue 331
        QStringLiteral("#1d1418"),   // codeInlineBackground
        QStringLiteral("#1c1c1c"),   // tabInactive (unselected tab; muted text 7.00:1)
        QStringLiteral("#707070"),   // border       4.24:1 (non-text)
        QStringLiteral("#1c1c1c"),   // tableHeader
    };
    return m == Dark ? black : white;
}

QString Theme::name(Mode m)
{
    return m == Dark ? QStringLiteral("黑色主題") : QStringLiteral("白色主題");
}

qreal Theme::headingPointSize(int level)
{
    switch (level) {
    case 1:  return 23.0;
    case 2:  return 17.0;
    case 3:  return 14.0;
    case 4:  return 12.5;
    case 5:  return 11.5;
    case 6:  return 11.0;
    default: return BodyPointSize;
    }
}

QString Theme::documentStyleSheet(Mode m)
{
    const Colors &c = colors(m);

    // Font sizes are **not** set here:
    //  * Headings — Qt applies its own fontSizeAdjustment to h5/h6, which CSS
    //    cannot override, so applyHeadingScale() sets them explicitly (see
    //    Theme::headingPointSize).
    //  * Body — measured, `body { font-size }` has **no effect at all** on a
    //    QTextDocument; body text stayed at the widget's system default until
    //    setDefaultFont() took over.
    // Only margins, line height and colours live here — Qt does honour those.
    //
    // Only the CSS subset Qt rich text supports: color, background-color,
    // font-*, margin, padding, text-decoration and table background-color.
    // flex, grid, max-width and border-left are all unsupported — the column
    // width comes from viewport margins, and the heading rules and blockquote
    // bar are painted by MdTextBrowser::paintEvent().
    //
    // Named tokens rather than %1/%2 on purpose: QString::arg's multi-argument
    // overload substitutes the *lowest-numbered markers present*, in order —
    // it does not map %N to the Nth argument. Skipping one number (say %4)
    // shifts everything after it. That actually happened here and left inline
    // code's background-color holding the foreground colour.
    //
    // 中：QString::arg 多引數會錯位，所以一律用具名 token。
    QString css = QStringLiteral(R"(
        body { color: @TEXT@; background-color: @BG@; line-height: @LH@%; }
        h1 { margin-top: 24px; margin-bottom: 12px; color: @TEXT@; }
        h2 { margin-top: 28px; margin-bottom: 12px; color: @TEXT@; }
        h3 { margin-top: 24px; margin-bottom: 8px; color: @TEXT@; }
        h4 { margin-top: 20px; margin-bottom: 6px; color: @TEXT@; }
        h5 { margin-top: 18px; margin-bottom: 6px; color: @TEXT@; }
        h6 { margin-top: 18px; margin-bottom: 6px; color: @MUTED@; }
        p  { margin-top: 10px; margin-bottom: 10px; line-height: @LH@%; }
        ul { margin-top: 4px; margin-bottom: 4px; }
        ol { margin-top: 4px; margin-bottom: 4px; }
        li { line-height: @LH@%; margin-top: 0px; margin-bottom: 0px; }
        td { line-height: @LH@%; }
        th { line-height: @LH@%; }
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
        { QStringLiteral("@LH@"), QString::number(int(LineHeightPercent)) },
    };
    for (const auto &t : tokens)
        css.replace(t.first, t.second);

    return css;
}

QString Theme::tabBarStyleSheet(Mode m)
{
    const Colors &c = colors(m);

    // The selected tab uses the page background, the body text colour and a top
    // accent line; unselected tabs sit one step back with secondary text. That
    // gives three cues for where focus is — background, text weight and the
    // accent line — rather than a single faint shade difference.
    QString qss = QStringLiteral(R"(
        QTabBar::tab {
            background: @INACTIVE@;
            color: @MUTED@;
            padding: 5px 10px;
            margin-right: 2px;
            border-top: 2px solid transparent;
            border-bottom: 1px solid @BORDER@;
        }
        QTabBar::tab:selected {
            background: @BG@;
            color: @TEXT@;
            border-top: 2px solid @LINK@;
            border-bottom: 1px solid @BG@;
            font-weight: bold;
        }
        QTabBar::tab:hover:!selected {
            color: @TEXT@;
        }
    )");

    const QList<QPair<QString, QString>> tokens{
        { QStringLiteral("@INACTIVE@"), c.tabInactive },
        { QStringLiteral("@MUTED@"), c.muted },
        { QStringLiteral("@BG@"), c.background },
        { QStringLiteral("@TEXT@"), c.text },
        { QStringLiteral("@LINK@"), c.link },
        { QStringLiteral("@BORDER@"), c.border },
    };
    for (const auto &t : tokens)
        qss.replace(t.first, t.second);
    return qss;
}

QPalette Theme::palette(Mode m)
{
    // Every role must be set. With only Window/Base/Text, QTabBar and QMenuBar
    // paint using the default Button/ButtonText (a light pair), which on the
    // black theme produced invisible tab labels and an invisible menu bar.
    //
    // 中：role 沒設滿，黑色主題下分頁標籤與選單列會整個消失。
    const Colors &c = colors(m);
    const QColor bg(c.background);
    const QColor text(c.text);
    const QColor muted(c.muted);
    const QColor link(c.link);
    const QColor chrome(c.tableHeader);      // Background for buttons and tabs
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

    // Greys used for bevelled frames
    p.setColor(QPalette::Light, m == Dark ? QColor(0x2a, 0x2a, 0x2a) : QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::Midlight, m == Dark ? QColor(0x22, 0x22, 0x22) : QColor(0xf2, 0xf2, 0xf2));
    p.setColor(QPalette::Mid, edge);
    p.setColor(QPalette::Dark, edge);
    p.setColor(QPalette::Shadow, m == Dark ? QColor(0x00, 0x00, 0x00) : QColor(0x8c, 0x8c, 0x8c));

    // Disabled state must still be visible (WCAG 3:1 for non-text)
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
