#include <QtTest>
#include <QCryptographicHash>

#include "core/MarkdownParser.h"

class TestMarkdownParser : public QObject
{
    Q_OBJECT

private slots:
    // ---- TOC ----
    void tocCapturesLevelsAndText();
    void tocSlugFollowsGithubRules();
    void tocSlugDeduplicates();
    void tocSlugKeepsCjk();
    void titleIsFirstH1();

    // ---- mermaid ----
    void mermaidFenceBecomesImagePlaceholder();
    void mermaidKeyIsSha1OfSource();
    void mermaidDisabledFallsBackToCodeBlock();

    // ---- Qt rich-text 子集的相容處理 ----
    void strikethroughUsesSTag();
    void taskListBecomesCheckboxGlyphs();
    void htmlSpecialCharsAreEscaped();

    // ---- 圖片 ----
    void relativeImageResolvesAgainstBaseDir();
    void absoluteImageUrlIsUntouched();

    // ---- 其他區塊 ----
    void tableIsRendered();
    void fencedCodeIsEscapedAndWrapped();
};

// ---------------------------------------------------------------- TOC

void TestMarkdownParser::tocCapturesLevelsAndText()
{
    const Document d = MarkdownParser::parse("# One\n\ntext\n\n## Two\n\n### Three\n");

    QCOMPARE(d.toc.size(), 3);
    QCOMPARE(d.toc[0].level, 1);
    QCOMPARE(d.toc[0].text, QStringLiteral("One"));
    QCOMPARE(d.toc[1].level, 2);
    QCOMPARE(d.toc[1].text, QStringLiteral("Two"));
    QCOMPARE(d.toc[2].level, 3);
    QCOMPARE(d.toc[2].text, QStringLiteral("Three"));
}

void TestMarkdownParser::tocSlugFollowsGithubRules()
{
    // 小寫、空白轉 '-'、去標點、保留 '-' 與 '_'
    const Document d = MarkdownParser::parse("## Hello, World! (v2)\n\n## snake_case-and-dash\n");

    QCOMPARE(d.toc.size(), 2);
    QCOMPARE(d.toc[0].anchor, QStringLiteral("hello-world-v2"));
    QCOMPARE(d.toc[1].anchor, QStringLiteral("snake_case-and-dash"));
}

void TestMarkdownParser::tocSlugDeduplicates()
{
    // GitHub 行為：第二次起加 -1, -2 …
    const Document d = MarkdownParser::parse("## Dup\n\n## Dup\n\n## Dup\n");

    QCOMPARE(d.toc.size(), 3);
    QCOMPARE(d.toc[0].anchor, QStringLiteral("dup"));
    QCOMPARE(d.toc[1].anchor, QStringLiteral("dup-1"));
    QCOMPARE(d.toc[2].anchor, QStringLiteral("dup-2"));
}

void TestMarkdownParser::tocSlugKeepsCjk()
{
    const Document d = MarkdownParser::parse("## 安裝說明\n\n## 使用 方式\n");

    QCOMPARE(d.toc.size(), 2);
    QCOMPARE(d.toc[0].anchor, QStringLiteral("安裝說明"));
    QCOMPARE(d.toc[1].anchor, QStringLiteral("使用-方式"));
}

void TestMarkdownParser::titleIsFirstH1()
{
    QCOMPARE(MarkdownParser::parse("## sub\n\n# Real Title\n\n# Second\n").title,
             QStringLiteral("Real Title"));
    QVERIFY(MarkdownParser::parse("## only sub\n").title.isEmpty());
}

// ------------------------------------------------------------ mermaid

void TestMarkdownParser::mermaidFenceBecomesImagePlaceholder()
{
    const QString md = "before\n\n```mermaid\nflowchart LR\n  A --> B\n```\n\nafter\n";
    const Document d = MarkdownParser::parse(md);

    QCOMPARE(d.mermaid.size(), 1);
    QCOMPARE(d.mermaid[0].source.trimmed(), QStringLiteral("flowchart LR\n  A --> B"));

    // 原位置換成 <img src="mermaid://<key>">
    QVERIFY2(d.html.contains("<img src=\"mermaid://" + d.mermaid[0].key + "\""),
             qPrintable("html 缺少 mermaid 佔位圖:\n" + d.html));
    // 不應同時留下程式碼區塊
    QVERIFY(!d.html.contains("flowchart LR"));
}

void TestMarkdownParser::mermaidKeyIsSha1OfSource()
{
    const QString md = "```mermaid\ngraph TD\n  X --> Y\n```\n";
    const Document d = MarkdownParser::parse(md);

    QCOMPARE(d.mermaid.size(), 1);
    const QByteArray expected =
        QCryptographicHash::hash(d.mermaid[0].source.toUtf8(), QCryptographicHash::Sha1).toHex();
    QCOMPARE(d.mermaid[0].key, QString::fromLatin1(expected));
}

void TestMarkdownParser::mermaidDisabledFallsBackToCodeBlock()
{
    MarkdownParser::Options opt;
    opt.mermaidEnabled = false;

    const Document d = MarkdownParser::parse("```mermaid\ngraph TD\n  X --> Y\n```\n", QString(), opt);

    QVERIFY(d.mermaid.isEmpty());
    QVERIFY(!d.html.contains("mermaid://"));
    QVERIFY2(d.html.contains("graph TD"), qPrintable(d.html));
}

// -------------------------------------------- Qt rich-text 子集相容

void TestMarkdownParser::strikethroughUsesSTag()
{
    // Qt rich-text 不認 <del>，必須是 <s>
    const Document d = MarkdownParser::parse("~~gone~~\n");

    QVERIFY2(d.html.contains("<s>gone</s>"), qPrintable(d.html));
    QVERIFY(!d.html.contains("<del>"));
}

void TestMarkdownParser::taskListBecomesCheckboxGlyphs()
{
    // Qt 不渲染 <input type=checkbox>，改用字元
    const Document d = MarkdownParser::parse("- [x] done\n- [ ] todo\n");

    QVERIFY(!d.html.contains("<input"));
    QVERIFY2(d.html.contains(QStringLiteral("☑")), qPrintable(d.html)); // ☑
    QVERIFY2(d.html.contains(QStringLiteral("☐")), qPrintable(d.html)); // ☐
}

void TestMarkdownParser::htmlSpecialCharsAreEscaped()
{
    const Document d = MarkdownParser::parse("a < b & c > d\n");

    QVERIFY2(d.html.contains("a &lt; b &amp; c &gt; d"), qPrintable(d.html));
}

// -------------------------------------------------------------- 圖片

void TestMarkdownParser::relativeImageResolvesAgainstBaseDir()
{
    const Document d = MarkdownParser::parse("![alt](sub/pic.png)\n", QStringLiteral("/tmp/base"));

    QVERIFY2(d.html.contains("src=\"file:///tmp/base/sub/pic.png\""), qPrintable(d.html));
}

void TestMarkdownParser::absoluteImageUrlIsUntouched()
{
    const Document d = MarkdownParser::parse("![a](https://example.com/y.png)\n",
                                             QStringLiteral("/tmp/base"));

    QVERIFY2(d.html.contains("src=\"https://example.com/y.png\""), qPrintable(d.html));
}

// ---------------------------------------------------------- 其他區塊

void TestMarkdownParser::tableIsRendered()
{
    const QString md = "| a | b |\n|---|---|\n| 1 | 2 |\n";
    const Document d = MarkdownParser::parse(md);

    QVERIFY2(d.html.contains("<table"), qPrintable(d.html));
    QVERIFY(d.html.contains("<th"));
    QVERIFY(d.html.contains("<td"));
}

void TestMarkdownParser::fencedCodeIsEscapedAndWrapped()
{
    const Document d = MarkdownParser::parse("```cpp\nif (a < b) { f(); }\n```\n");

    QVERIFY2(d.html.contains("<pre"), qPrintable(d.html));
    QVERIFY2(d.html.contains("a &lt; b"), qPrintable(d.html));
}

QTEST_APPLESS_MAIN(TestMarkdownParser)
#include "test_markdownparser.moc"
