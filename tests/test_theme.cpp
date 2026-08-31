#include <QtTest>
#include <QRegularExpression>

#include "Theme.h"
#include "core/CodeHighlighter.h"

/// 主題對比保證。
///
/// 需求是「content 必須是對比色，不要有看不見的狀況」，所以這裡不靠眼睛，
/// 而是用 WCAG 2.1 相對亮度把每一組顏色的對比比算出來釘住門檻。
/// 任何人改配色而打破門檻，這支測試就會失敗。
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
    // 中灰應落在兩者之間
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
    // 框線是非文字元素，WCAG 門檻是 3:1
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
    // 程式碼區塊要看得出是一塊，但不能反過來搶對比
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
    // 黑箱做法：真的產出高亮 HTML，把裡面每一個 color 抽出來，
    // 對著同一段 HTML 宣告的 <pre> 背景色算對比。
    // 這樣連日後新增語言都會自動被檢查，不需要另外開 API。
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

void TestTheme::paletteRolePairsAreReadable()
{
    // widget chrome 也算「content 不能看不見」的一部分：
    // 只設 Window/Base/Text 時，QTabBar 與 QMenuBar 會用預設的
    // Button/ButtonText 去畫，黑色主題下就是隱形的分頁標籤與選單。
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

        // 停用狀態也要看得見
        const double dis = Theme::contrastRatio(
            p.color(QPalette::Disabled, QPalette::WindowText),
            p.color(QPalette::Disabled, QPalette::Window));
        QVERIFY2(dis >= Theme::MinNonTextContrast,
                 qPrintable(QStringLiteral("%1 的停用文字只有 %2:1").arg(Theme::name(m)).arg(dis)));
    }
}

void TestTheme::paletteHasNoDefaultLightRolesInBlackTheme()
{
    // 直接盯住「忘記設某個 role」這個 bug：黑色主題下每一個背景類 role
    // 都必須是深色，否則就是預設的淺色系漏進來了。
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
    // 黑色主題的正文色是淺色；若背景是白的（例如 markdown 內嵌了
    // background-color:#fff 的原始 HTML），必須退回深色而不是硬用主題色。
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
    // 掃一遍灰階與幾個彩色，任何背景都必須拿到可讀的前景
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
