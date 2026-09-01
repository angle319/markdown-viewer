#include <QtTest>

#include "core/CodeHighlighter.h"

class TestCodeHighlighter : public QObject
{
    Q_OBJECT

private slots:
    void wrapsInPre();
    void escapesHtml();
    void knownLanguagesAreSupported();
    void unknownLanguageFallsBackWithoutColoring();
    void keywordsAreColored();
    void lineCommentIsColored();
    void unterminatedStringDoesNotSwallowRest();
    void darkAndLightPalettesDiffer();
};

void TestCodeHighlighter::wrapsInPre()
{
    const QString out = CodeHighlighter::highlight(QStringLiteral("x = 1\n"), QStringLiteral("python"));
    QVERIFY2(out.startsWith(QStringLiteral("<pre")), qPrintable(out));
    QVERIFY(out.endsWith(QStringLiteral("</pre>")));
}

void TestCodeHighlighter::escapesHtml()
{
    const QString out = CodeHighlighter::highlight(
        QStringLiteral("if (a < b && c > d) { }"), QStringLiteral("cpp"));

    QVERIFY2(out.contains(QStringLiteral("&lt;")), qPrintable(out));
    QVERIFY(out.contains(QStringLiteral("&gt;")));
    QVERIFY(out.contains(QStringLiteral("&amp;")));
    // No unescaped tag may survive
    QVERIFY(!out.contains(QStringLiteral("<b>")));
}

void TestCodeHighlighter::knownLanguagesAreSupported()
{
    for (const QString &l : { QStringLiteral("cpp"), QStringLiteral("c"), QStringLiteral("python"),
                              QStringLiteral("js"), QStringLiteral("typescript"),
                              QStringLiteral("json"), QStringLiteral("bash"),
                              QStringLiteral("cmake") })
        QVERIFY2(CodeHighlighter::supports(l), qPrintable(l));

    QVERIFY(!CodeHighlighter::supports(QStringLiteral("brainfuck")));
    QVERIFY(!CodeHighlighter::supports(QString()));
}

void TestCodeHighlighter::unknownLanguageFallsBackWithoutColoring()
{
    const QString out = CodeHighlighter::highlight(
        QStringLiteral("some plain <text>"), QStringLiteral("nosuchlang"));

    QVERIFY(out.contains(QStringLiteral("&lt;text&gt;")));
    QVERIFY2(!out.contains(QStringLiteral("<span")), qPrintable(out));
}

void TestCodeHighlighter::keywordsAreColored()
{
    const QString out = CodeHighlighter::highlight(
        QStringLiteral("return nullptr;"), QStringLiteral("cpp"));

    QVERIFY2(out.contains(QStringLiteral("<span style=\"color:")), qPrintable(out));
    QVERIFY(out.contains(QStringLiteral(">return</span>")));
    QVERIFY(out.contains(QStringLiteral(">nullptr</span>")));
}

void TestCodeHighlighter::lineCommentIsColored()
{
    const QString out = CodeHighlighter::highlight(
        QStringLiteral("# comment\nx = 1\n"), QStringLiteral("python"));

    QVERIFY2(out.contains(QStringLiteral("font-style:italic")), qPrintable(out));
    QVERIFY(out.contains(QStringLiteral("# comment")));
}

void TestCodeHighlighter::unterminatedStringDoesNotSwallowRest()
{
    // A missing quote must not swallow the rest of the file as string content
    const QString out = CodeHighlighter::highlight(
        QStringLiteral("a = \"oops\nreturn 1;\n"), QStringLiteral("cpp"));

    QVERIFY2(out.contains(QStringLiteral(">return</span>")), qPrintable(out));
}

void TestCodeHighlighter::darkAndLightPalettesDiffer()
{
    const QString code = QStringLiteral("return 1;");
    QVERIFY(CodeHighlighter::highlight(code, QStringLiteral("cpp"), false)
            != CodeHighlighter::highlight(code, QStringLiteral("cpp"), true));
}

QTEST_APPLESS_MAIN(TestCodeHighlighter)
#include "test_codehighlighter.moc"
