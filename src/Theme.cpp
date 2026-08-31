#include "Theme.h"

#include <QColor>

const Theme::Colors &Theme::colors(Mode m)
{
    static const Colors light{
        QStringLiteral("#ffffff"), QStringLiteral("#24292f"), QStringLiteral("#57606a"),
        QStringLiteral("#0969da"), QStringLiteral("#f6f8fa"), QStringLiteral("#d0d7de"),
        QStringLiteral("#f6f8fa"),
    };
    static const Colors dark{
        QStringLiteral("#1f2430"), QStringLiteral("#d7dae0"), QStringLiteral("#8b949e"),
        QStringLiteral("#6cb6ff"), QStringLiteral("#161b22"), QStringLiteral("#30363d"),
        QStringLiteral("#22272e"),
    };
    return m == Dark ? dark : light;
}

QString Theme::documentStyleSheet(Mode m)
{
    const Colors &c = colors(m);

    // 只用 Qt rich-text 支援的 CSS 屬性子集：color、background-color、
    // font-*、margin、padding、border（表格）、text-decoration。
    // flex / grid / max-width 都不支援，欄寬靠 viewport margin 處理。
    return QStringLiteral(R"(
        body { color: %1; background-color: %2; }
        h1 { font-size: 24pt; margin-top: 18px; margin-bottom: 10px; }
        h2 { font-size: 19pt; margin-top: 16px; margin-bottom: 8px; }
        h3 { font-size: 16pt; margin-top: 14px; margin-bottom: 6px; }
        h4, h5, h6 { font-size: 13pt; margin-top: 12px; margin-bottom: 6px; }
        p  { margin-top: 6px; margin-bottom: 6px; }
        a  { color: %3; text-decoration: none; }
        code { background-color: %4; font-family: monospace; }
        blockquote { color: %5; margin-left: 12px; padding-left: 10px; }
        th { background-color: %6; }
        hr { color: %7; }
    )")
        .arg(c.text, c.background, c.link, c.codeBackground, c.muted, c.tableHeader, c.border);
}

QPalette Theme::palette(Mode m)
{
    const Colors &c = colors(m);
    QPalette p;
    p.setColor(QPalette::Base, QColor(c.background));
    p.setColor(QPalette::Window, QColor(c.background));
    p.setColor(QPalette::Text, QColor(c.text));
    p.setColor(QPalette::WindowText, QColor(c.text));
    p.setColor(QPalette::Link, QColor(c.link));
    return p;
}
