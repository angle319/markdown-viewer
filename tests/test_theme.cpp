#include <QtTest>
#include <QRegularExpression>

#include "Theme.h"
#include "core/CodeHighlighter.h"

/// Theme contrast guarantees.
///
/// The requirement was "content must contrast; nothing may be invisible", so
/// this does not rely on eyes: every colour pair is expressed as a WCAG 2.1
/// contrast ratio and pinned to a threshold. Changing a colour that breaks one
/// fails this test.
class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void relativeLuminanceHasKnownEndpoints();
    void contrastRatioBlackOnWhiteIs21();
    void contrastRatioIsSymmetricAndSelfIsOne();

    void themesAreActuallyWhiteAndBlack();
    void bodyTextMeetsAaa();
    void mutedAndLinkMeetAa();
    void borderMeetsNonTextThreshold();
    void codeAndTableBackgroundsKeepTextReadable();
    void codeBackgroundIsDistinguishableFromPage();

    void syntaxColoursAreReadable();

    void styleSheetHasNoLeftoverPlaceholders();
    void inlineCodeColoursAreReadableAndDistinct();
    void tabBarStatesAreClearlyDistinct();
    void paletteRolePairsAreReadable();
    void paletteHasNoDefaultLightRolesInBlackTheme();

    void readableOnFallsBackWhenThemeTextWouldBeInvisible();
    void readableOnAlwaysReturnsSomethingReadable();

private:
    static QList<Theme::Mode> modes() { return { Theme::Light, Theme::Dark }; }
};

void TestTheme::relativeLuminanceHasKnownEndpoints()
{
    QVERIFY(qFuzzyIsNull(Theme::relativeLuminance(Qt::black)));
    QVERIFY(qAbs(Theme::relativeLuminance(Qt::white) - 1.0) < 1e-9);
    // Mid grey should land between the two
    const double mid = Theme::relativeLuminance(QColor(128, 128, 128));
    QVERIFY(mid > 0.15 && mid < 0.25);
}

void TestTheme::contrastRatioBlackOnWhiteIs21()
{
    QVERIFY(qAbs(Theme::contrastRatio(Qt::black, Qt::white) - 21.0) < 0.01);
}

void TestTheme::contrastRatioIsSymmetricAndSelfIsOne()
{
    const QColor a(0x0b, 0x57, 0xd0);
    const QColor b(0xff, 0xff, 0xff);
    QVERIFY(qAbs(Theme::contrastRatio(a, b) - Theme::contrastRatio(b, a)) < 1e-12);
    QVERIFY(qAbs(Theme::contrastRatio(a, a) - 1.0) < 1e-12);
}

void TestTheme::themesAreActuallyWhiteAndBlack()
{
    QCOMPARE(Theme::colors(Theme::Light).background, QStringLiteral("#ffffff"));
    QCOMPARE(Theme::colors(Theme::Dark).background, QStringLiteral("#000000"));
    QCOMPARE(Theme::name(Theme::Light), QStringLiteral("白色主題"));
    QCOMPARE(Theme::name(Theme::Dark), QStringLiteral("黑色主題"));
}

void TestTheme::bodyTextMeetsAaa()
{
    for (Theme::Mode m : modes()) {
        const Theme::Colors &c = Theme::colors(m);
        const double r = Theme::contrastRatio(QColor(c.text), QColor(c.background));
        QVERIFY2(r >= Theme::MinBodyTextContrast,
                 qPrintable(QStringLiteral("%1 的正文對比只有 %2:1（需 %3）")
                                .arg(Theme::name(m)).arg(r).arg(Theme::MinBodyTextContrast)));
    }
}

void TestTheme::mutedAndLinkMeetAa()
{
    for (Theme::Mode m : modes()) {
        const Theme::Colors &c = Theme::colors(m);
        const QColor bg(c.background);
        for (const auto &pair : QList<QPair<QString, QString>>{
                 { QStringLiteral("muted"), c.muted }, { QStringLiteral("link"), c.link } }) {
            const double r = Theme::contrastRatio(QColor(pair.second), bg);
            QVERIFY2(r >= Theme::MinTextContrast,
                     qPrintable(QStringLiteral("%1 的 %2 對比只有 %3:1（需 %4）")
                                    .arg(Theme::name(m), pair.first)
                                    .arg(r).arg(Theme::MinTextContrast)));
        }
    }
}

void TestTheme::borderMeetsNonTextThreshold()
{
    // Borders are non-text elements, so the WCAG threshold is 3:1
    for (Theme::Mode m : modes()) {
        const Theme::Colors &c = Theme::colors(m);
        const double r = Theme::contrastRatio(QColor(c.border), QColor(c.background));
        QVERIFY2(r >= Theme::MinNonTextContrast,
                 qPrintable(QStringLiteral("%1 的框線對比只有 %2:1（需 %3）")
                                .arg(Theme::name(m)).arg(r).arg(Theme::MinNonTextContrast)));
    }
}

void TestTheme::codeAndTableBackgroundsKeepTextReadable()
{
    for (Theme::Mode m : modes()) {
        const Theme::Colors &c = Theme::colors(m);
        const QColor text(c.text);
        for (const auto &pair : QList<QPair<QString, QString>>{
                 { QStringLiteral("codeBackground"), c.codeBackground },
                 { QStringLiteral("tableHeader"), c.tableHeader } }) {
            const double r = Theme::contrastRatio(text, QColor(pair.second));
            QVERIFY2(r >= Theme::MinBodyTextContrast,
                     qPrintable(QStringLiteral("%1 在 %2 上的正文對比只有 %3:1（需 %4）")
                                    .arg(Theme::name(m), pair.first)
                                    .arg(r).arg(Theme::MinBodyTextContrast)));
        }
    }
}

void TestTheme::codeBackgroundIsDistinguishableFromPage()
{
    // A code block should read as a block without stealing contrast itself
    for (Theme::Mode m : modes()) {
        const Theme::Colors &c = Theme::colors(m);
        const double r = Theme::contrastRatio(QColor(c.codeBackground), QColor(c.background));
        QVERIFY2(r > 1.03 && r < 2.0,
                 qPrintable(QStringLiteral("%1 的程式碼底色與頁面底色對比 %2:1 不合理")
                                .arg(Theme::name(m)).arg(r)));
    }
}

void TestTheme::syntaxColoursAreReadable()
{
    // Black-box approach: actually produce highlighted HTML, pull every colour
    // out of it, and measure against the <pre> background declared in the same
    // output. Languages added later are then checked automatically, with no
    // extra API to maintain.
    const QStringList langs{
        QStringLiteral("cpp"),  QStringLiteral("c"),    QStringLiteral("python"),
        QStringLiteral("js"),   QStringLiteral("ts"),   QStringLiteral("json"),
        QStringLiteral("bash"), QStringLiteral("cmake"),
    };
    const QString code = QStringLiteral(
        "// comment line\n"
        "#include <stdio.h>\n"
        "def f(x): return \"str\" + 'c' + 42 * 0x1f\n"
        "if (a < b) { return nullptr; } /* block */\n"
        "true false null add_executable REQUIRED\n"
        "echo \"hi\"  # trailing\n");

    static const QRegularExpression preBgRe(
        QStringLiteral("<pre style=\"background-color:(#[0-9a-fA-F]{6})"));
    static const QRegularExpression colourRe(
        QStringLiteral("color:(#[0-9a-fA-F]{6})"));

    int checked = 0;
    for (bool dark : { false, true }) {
        for (const QString &lang : langs) {
            const QString html = CodeHighlighter::highlight(code, lang, dark);

            const auto bgMatch = preBgRe.match(html);
            QVERIFY2(bgMatch.hasMatch(), qPrintable(html.left(120)));
            const QColor bg(bgMatch.captured(1));
            QVERIFY(bg.isValid());

            auto it = colourRe.globalMatch(html);
            int seen = 0;
            while (it.hasNext()) {
                const QColor fg(it.next().captured(1));
                QVERIFY(fg.isValid());
                if (fg == bg)
                    continue;   // <pre> 自己的 background-color 也會被這個 regex 抓到
                const double r = Theme::contrastRatio(fg, bg);
                QVERIFY2(r >= Theme::MinTextContrast,
                         qPrintable(QStringLiteral("%1 / %2：%3 在 %4 上只有 %5:1（需 %6）")
                                        .arg(dark ? QStringLiteral("黑") : QStringLiteral("白"),
                                             lang, fg.name(), bg.name())
                                        .arg(r).arg(Theme::MinTextContrast)));
                ++seen;
                ++checked;
            }
            QVERIFY2(seen > 0, qPrintable(QStringLiteral("%1 完全沒有著色").arg(lang)));
        }
    }
    qInfo() << "檢查了" << checked << "組語法高亮顏色";
}

void TestTheme::styleSheetHasNoLeftoverPlaceholders()
{
    // This guards a trap that was actually hit: QString::arg's multi-argument
    // overload substitutes the lowest-numbered markers *present*, in order — it
    // does not map %N to the Nth argument. Skipping one number shifts everything
    // after it. At the time that left inline code's background holding the
    // foreground colour, and a loose assertion still passed because the contrast
    // fixup replaced the foreground with something readable. Named tokens are
    // used now; this checks the substitution is clean and that nobody went back
    // to positional arguments.
    for (Theme::Mode m : modes()) {
        const QString css = Theme::documentStyleSheet(m);
        QVERIFY2(!css.contains(QLatin1Char('@')),
                 qPrintable(QStringLiteral("stylesheet 有未取代的 token: %1").arg(css)));
        QVERIFY2(!css.contains(QRegularExpression(QStringLiteral("%\\d"))),
                 qPrintable(QStringLiteral("stylesheet 有未取代的位置引數: %1").arg(css)));

        // Every colour should actually appear in the CSS
        const Theme::Colors &c = Theme::colors(m);
        for (const QString &colour : { c.background, c.text, c.muted, c.link,
                                       c.codeInline, c.codeInlineBackground,
                                       c.border, c.tableHeader })
            QVERIFY2(css.contains(colour),
                     qPrintable(QStringLiteral("%1 沒出現在 stylesheet 裡").arg(colour)));
    }
}

void TestTheme::inlineCodeColoursAreReadableAndDistinct()
{
    for (Theme::Mode m : modes()) {
        const Theme::Colors &c = Theme::colors(m);
        const QColor fg(c.codeInline);
        const QColor bg(c.codeInlineBackground);
        const QColor pageBg(c.background);
        const QColor link(c.link);

        const double r = Theme::contrastRatio(fg, bg);
        QVERIFY2(r >= Theme::MinTextContrast,
                 qPrintable(QStringLiteral("%1 行內 code 對比 %2:1").arg(Theme::name(m)).arg(r)));

        // The background should read as a chip without stealing contrast
        const double chip = Theme::contrastRatio(bg, pageBg);
        QVERIFY2(chip > 1.05 && chip < 2.0,
                 qPrintable(QStringLiteral("%1 行內 code 底色與頁面對比 %2:1 不合理")
                                .arg(Theme::name(m)).arg(chip)));

        // It must be distinguishable from the link colour, or the reader cannot
        // tell code from a clickable link.
        //
        // The metric is **hue difference**, not WCAG contrast: contrast measures
        // luminance only, so purple (#6f42c1) and blue (#0b57d0) score 1.13:1
        // despite being obviously different. Using contrast here would push the
        // fix toward changing lightness rather than hue, which is the wrong
        // direction.
        const auto hueDelta = [](const QColor &a, const QColor &b) {
            const int ha = a.hue();
            const int hb = b.hue();
            if (ha < 0 || hb < 0)
                return 180.0;   // 灰階視為完全不同色相
            const double d = qAbs(double(ha) - double(hb));
            return qMin(d, 360.0 - d);
        };
        const double dHue = hueDelta(fg, link);
        QVERIFY2(fg != link && dHue >= 60.0,
                 qPrintable(QStringLiteral("%1 行內 code 與連結的色相只差 %2°（需 ≥60）: %3 vs %4")
                                .arg(Theme::name(m)).arg(dHue).arg(fg.name(), link.name())));

        // It must also differ from body text, or nothing is marked at all
        QVERIFY(fg != QColor(c.text));
    }
}

void TestTheme::tabBarStatesAreClearlyDistinct()
{
    // Reported by a user: "the tab highlight is too weak, I cannot tell where
    // focus is". QTabBar's default selected state differs by only a slight
    // background shade, so a stylesheet separates them explicitly:
    // selected = page background + body text + bold + top accent line;
    // unselected = one step back + secondary text.
    for (Theme::Mode m : modes()) {
        const Theme::Colors &c = Theme::colors(m);

        const double selected = Theme::contrastRatio(QColor(c.text), QColor(c.background));
        const double unselected = Theme::contrastRatio(QColor(c.muted), QColor(c.tabInactive));
        QVERIFY2(selected >= Theme::MinBodyTextContrast,
                 qPrintable(QStringLiteral("%1 選取中分頁的文字對比只有 %2:1")
                                .arg(Theme::name(m)).arg(selected)));
        QVERIFY2(unselected >= Theme::MinTextContrast,
                 qPrintable(QStringLiteral("%1 未選取分頁的文字對比只有 %2:1")
                                .arg(Theme::name(m)).arg(unselected)));

        // The two backgrounds must be tellable apart, or focus is invisible
        const double states = Theme::contrastRatio(QColor(c.background), QColor(c.tabInactive));
        QVERIFY2(states >= 1.2,
                 qPrintable(QStringLiteral("%1 選取／未選取的底色只差 %2，分不出來")
                                .arg(Theme::name(m)).arg(states)));

        // Selected text must be more prominent than unselected
        QVERIFY2(selected > unselected,
                 qPrintable(QStringLiteral("%1 選取中的文字沒有比未選取的更顯眼")
                                .arg(Theme::name(m))));

        // The accent line must be visible
        const double accent = Theme::contrastRatio(QColor(c.link), QColor(c.background));
        QVERIFY2(accent >= Theme::MinNonTextContrast,
                 qPrintable(QStringLiteral("%1 分頁強調線看不清楚: %2:1")
                                .arg(Theme::name(m)).arg(accent)));

        // The stylesheet must have every token substituted
        const QString qss = Theme::tabBarStyleSheet(m);
        QVERIFY2(!qss.contains(QLatin1Char('@')), qPrintable(qss));
        QVERIFY(qss.contains(c.tabInactive));
        QVERIFY(qss.contains(c.link));
    }
}

void TestTheme::paletteRolePairsAreReadable()
{
    // Widget chrome counts as content that must not be invisible: with only
    // Window/Base/Text set, QTabBar and QMenuBar paint with the default
    // Button/ButtonText, which on the black theme means invisible tab labels
    // and an invisible menu bar.
    struct Pair {
        const char *name;
        QPalette::ColorRole fg;
        QPalette::ColorRole bg;
        double need;
    };
    static const Pair pairs[] = {
        { "WindowText / Window",        QPalette::WindowText,      QPalette::Window,      4.5 },
        { "Text / Base",                QPalette::Text,            QPalette::Base,        7.0 },
        { "Text / AlternateBase",       QPalette::Text,            QPalette::AlternateBase, 7.0 },
        { "ButtonText / Button",        QPalette::ButtonText,      QPalette::Button,      4.5 },
        { "ToolTipText / ToolTipBase",  QPalette::ToolTipText,     QPalette::ToolTipBase, 4.5 },
        { "HighlightedText / Highlight", QPalette::HighlightedText, QPalette::Highlight,  4.5 },
        { "Link / Base",                QPalette::Link,            QPalette::Base,        4.5 },
        { "PlaceholderText / Base",     QPalette::PlaceholderText, QPalette::Base,        3.0 },
        { "Mid / Window (非文字)",        QPalette::Mid,             QPalette::Window,      3.0 },
    };

    for (Theme::Mode m : modes()) {
        const QPalette p = Theme::palette(m);
        for (const Pair &pr : pairs) {
            const double r = Theme::contrastRatio(p.color(pr.fg), p.color(pr.bg));
            QVERIFY2(r >= pr.need,
                     qPrintable(QStringLiteral("%1 的 %2 只有 %3:1（需 %4；%5 on %6）")
                                    .arg(Theme::name(m), QString::fromLatin1(pr.name))
                                    .arg(r).arg(pr.need)
                                    .arg(p.color(pr.fg).name(), p.color(pr.bg).name())));
        }

        // The disabled state must stay visible too
        const double dis = Theme::contrastRatio(
            p.color(QPalette::Disabled, QPalette::WindowText),
            p.color(QPalette::Disabled, QPalette::Window));
        QVERIFY2(dis >= Theme::MinNonTextContrast,
                 qPrintable(QStringLiteral("%1 的停用文字只有 %2:1").arg(Theme::name(m)).arg(dis)));
    }
}

void TestTheme::paletteHasNoDefaultLightRolesInBlackTheme()
{
    // Guards the "forgot to set a role" bug directly: on the black theme every
    // background role must be dark, otherwise a default light one leaked in.
    const QPalette p = Theme::palette(Theme::Dark);
    for (QPalette::ColorRole role : { QPalette::Window, QPalette::Base, QPalette::AlternateBase,
                                      QPalette::Button, QPalette::ToolTipBase }) {
        const QColor c = p.color(role);
        QVERIFY2(Theme::relativeLuminance(c) < 0.2,
                 qPrintable(QStringLiteral("黑色主題的背景 role 竟然是亮色: %1").arg(c.name())));
    }
    for (QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text,
                                      QPalette::ButtonText, QPalette::ToolTipText }) {
        const QColor c = p.color(role);
        QVERIFY2(Theme::relativeLuminance(c) > 0.3,
                 qPrintable(QStringLiteral("黑色主題的文字 role 竟然是暗色: %1").arg(c.name())));
    }
}

void TestTheme::readableOnFallsBackWhenThemeTextWouldBeInvisible()
{
    // The black theme's text colour is light. On a white background — say raw
    // HTML in the markdown carrying background-color:#fff — it must fall back to
    // a dark colour instead of insisting on the theme colour.
    const QColor onWhite = Theme::readableOn(Qt::white, Theme::Dark);
    QVERIFY2(Theme::contrastRatio(onWhite, Qt::white) >= Theme::MinTextContrast,
             qPrintable(onWhite.name()));
    QVERIFY(Theme::relativeLuminance(onWhite) < 0.5);

    const QColor onBlack = Theme::readableOn(Qt::black, Theme::Light);
    QVERIFY2(Theme::contrastRatio(onBlack, Qt::black) >= Theme::MinTextContrast,
             qPrintable(onBlack.name()));
    QVERIFY(Theme::relativeLuminance(onBlack) > 0.5);
}

void TestTheme::readableOnAlwaysReturnsSomethingReadable()
{
    // Sweep the greys plus a few saturated colours: every background must yield
    // a readable foreground
    for (Theme::Mode m : modes()) {
        for (int v = 0; v <= 255; v += 5) {
            const QColor bg(v, v, v);
            const double r = Theme::contrastRatio(Theme::readableOn(bg, m), bg);
            QVERIFY2(r >= Theme::MinTextContrast,
                     qPrintable(QStringLiteral("灰階 %1 只拿到 %2:1").arg(v).arg(r)));
        }
        for (const QColor &bg : { QColor(0x0b, 0x57, 0xd0), QColor(0xff, 0xb1, 0x82),
                                  QColor(0x20, 0x69, 0x3f), QColor(0x9b, 0x14, 0x14) }) {
            const double r = Theme::contrastRatio(Theme::readableOn(bg, m), bg);
            QVERIFY2(r >= Theme::MinTextContrast,
                     qPrintable(QStringLiteral("%1 只拿到 %2:1").arg(bg.name()).arg(r)));
        }
    }
}

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
