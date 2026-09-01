#include <QtTest>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QScrollBar>
#include <QTextFragment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QTextFrame>
#include <QTextTable>
#include <QTextTableCell>
#include <QTreeWidget>

#include "DocumentView.h"
#include "MainWindow.h"
#include "Theme.h"
#include "render/TextBrowserBackend.h"
#include "core/MarkdownParser.h"

#ifndef SAMPLE_MD
#  error "SAMPLE_MD 未定義（應由 CMake 提供 docs/sample.md 的路徑）"
#endif
#ifndef HEADINGS_MD
#  error "HEADINGS_MD 未定義（應由 CMake 提供 docs/headings.md 的路徑）"
#endif

/// Regression suite: uses docs/sample.md as a syntax corpus and pins the
/// invariants of the whole pipeline.
///
/// Two layers:
///   1. Parsing — checks MarkdownParser's output directly (anchors, mermaid,
///      escaping, ...)
///   2. Presentation — checks that QTextDocument actually kept those structures
///      (headings, tables, code, ...)
///
/// Layer 2 exists because layer 1 passing does not mean the screen is right: Qt
/// rich text accepts only a subset of HTML, so "emitted correct HTML" and "Qt
/// preserved that structure" are two different claims.
class TestE2eRegression : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // --- Parsing layer ---
    void sampleFileExists();
    void tocAnchorsMatchGolden();
    void duplicateHeadingsGetGithubStyleSuffixes();
    void mermaidBlocksAreExtracted();
    void rawHtmlIsEscapedInsideCodeBlocks();
    void unsupportedTagsAreRewritten();

    // --- Presentation layer ---
    void headingBlocksSurviveQtHtmlParser();
    void tableSurvivesAsQTextTable();
    void codeBlocksKeepMonospaceAndPreserveWhitespace();
    void taskListGlyphsAppearInPlainText();
    void scriptTagShowsAsLiteralText();
    void missingImageIsReplacedByLabel();
    void everyImageFragmentHasPositiveSize();
    void contentColumnIsCappedAndCentred();

    // --- State changes preserve the invariants ---
    void themeRoundTripPreservesStructure();
    void reloadPreservesStructure();
    void inlineCodeIsVisuallyDistinct();
    void blockquoteMarkerContractHolds();
    void tableUsesHorizontalRulesOnly();
    void headingSizesFollowThemeScale();
    void bodyTextUsesComfortableLineHeight();
    void zoomScalesBodyAndHeadingsTogether();
    void zoomIsClampedAndResettable();
    void zoomShortcutsIncludeCtrlEquals();
    void wideTableOpensQuickly();
    void linksAreUnderlinedAndUseLinkColour();
    void everyTextFragmentIsReadableInBothThemes();
    void hardcodedColoursInSampleAreCorrected();

    // --- Optional visual output ---
    void dumpScreenshotsIfRequested();

private:
    QTextBrowser *browser() const
    {
        // With tabs, m_win->findChild is wrong: it returns the first view ever
        // created, which is not necessarily the active one
        DocumentView *v = m_win->activeView();
        return v ? v->findChild<QTextBrowser *>() : nullptr;
    }
    QAction *actionNamed(const QString &text) const;
    int headingBlockCount() const;

    /// Scans the document and returns every text fragment with insufficient
    /// contrast (text, foreground, background and ratio). The effective
    /// background is resolved the same way the render backend does it:
    /// fragment background, then block background, then the page colour.
    QStringList lowContrastFragments(Theme::Mode mode) const;
    int tableCount() const;

    QString m_markdown;
    Document m_doc;
    QScopedPointer<MainWindow> m_win;
};

void TestE2eRegression::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                              + QStringLiteral("/markdown-tool");
    if (QDir(cacheRoot).exists())
        QVERIFY(QDir(cacheRoot).removeRecursively());

    QFile f(QString::fromUtf8(SAMPLE_MD));
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(QString::fromUtf8(SAMPLE_MD)));
    m_markdown = QString::fromUtf8(f.readAll());
    QVERIFY(!m_markdown.isEmpty());

    m_doc = MarkdownParser::parse(m_markdown, QFileInfo(QString::fromUtf8(SAMPLE_MD)).absolutePath());

    m_win.reset(new MainWindow);
    m_win->resize(1400, 800);
    m_win->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_win.data()));
    QVERIFY(m_win->openFile(QString::fromUtf8(SAMPLE_MD)));
}

void TestE2eRegression::cleanupTestCase()
{
    m_win.reset();
}

int TestE2eRegression::headingBlockCount() const
{
    int n = 0;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (b.blockFormat().headingLevel() > 0)
            ++n;
    return n;
}

QAction *TestE2eRegression::actionNamed(const QString &text) const
{
    for (QAction *a : m_win->findChildren<QAction *>())
        if (a->text() == text)
            return a;
    return nullptr;
}

QStringList TestE2eRegression::lowContrastFragments(Theme::Mode mode) const
{
    const QColor pageBg(Theme::colors(mode).background);
    const QColor themeText(Theme::colors(mode).text);

    QStringList bad;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        QColor blockBg = pageBg;
        if (b.blockFormat().background().style() != Qt::NoBrush) {
            const QColor c = b.blockFormat().background().color();
            if (c.isValid() && c.alpha() > 0)
                blockBg = c;
        }

        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || f.charFormat().isImageFormat())
                continue;
            if (f.text().trimmed().isEmpty())
                continue;

            const QTextCharFormat cf = f.charFormat();
            QColor bg = blockBg;
            if (cf.background().style() != Qt::NoBrush) {
                const QColor c = cf.background().color();
                if (c.isValid() && c.alpha() > 0)
                    bg = c;
            }
            const QColor fg = cf.foreground().style() != Qt::NoBrush
                                  ? cf.foreground().color()
                                  : themeText;

            const double r = Theme::contrastRatio(fg, bg);
            if (r < Theme::MinTextContrast)
                bad << QStringLiteral("\"%1\" %2 on %3 = %4:1")
                           .arg(f.text().left(24), fg.name(), bg.name())
                           .arg(r, 0, 'f', 2);
        }
    }
    return bad;
}

int TestE2eRegression::tableCount() const
{
    int n = 0;
    QTextFrame *root = browser()->document()->rootFrame();
    for (QTextFrame *child : root->childFrames())
        if (qobject_cast<QTextTable *>(child))
            ++n;
    return n;
}

// ------------------------------------------------------------ Parsing layer

void TestE2eRegression::sampleFileExists()
{
    QVERIFY(QFile::exists(QString::fromUtf8(SAMPLE_MD)));
    QVERIFY(m_markdown.contains(QStringLiteral("```mermaid")));
}

void TestE2eRegression::tocAnchorsMatchGolden()
{
    // Anchors are an external contract — copied links rely on them — so any
    // change must be deliberate
    const QStringList golden{
        QStringLiteral("markdown-tool-驗證文件"),
        QStringLiteral("行內樣式"),
        QStringLiteral("清單與待辦"),
        QStringLiteral("引用"),
        QStringLiteral("表格"),
        QStringLiteral("程式碼"),
        QStringLiteral("mermaid"),
        QStringLiteral("標題重複測試"),
        QStringLiteral("重複"),
        QStringLiteral("重複-1"),
        QStringLiteral("重複-2"),
        QStringLiteral("對比保護"),
        QStringLiteral("圖片"),
        QStringLiteral("分隔線"),
    };

    QStringList actual;
    for (const TocEntry &e : m_doc.toc)
        actual << e.anchor;

    QCOMPARE(actual, golden);
    QCOMPARE(m_doc.title, QStringLiteral("markdown-tool 驗證文件"));
}

void TestE2eRegression::duplicateHeadingsGetGithubStyleSuffixes()
{
    QStringList dups;
    for (const TocEntry &e : m_doc.toc)
        if (e.text == QStringLiteral("重複"))
            dups << e.anchor;

    QCOMPARE(dups, QStringList({ QStringLiteral("重複"), QStringLiteral("重複-1"),
                                 QStringLiteral("重複-2") }));
}

void TestE2eRegression::mermaidBlocksAreExtracted()
{
    QCOMPARE(m_doc.mermaid.size(), 2);
    QVERIFY(m_doc.mermaid[0].source.contains(QStringLiteral("flowchart LR")));
    QVERIFY(m_doc.mermaid[1].source.contains(QStringLiteral("sequenceDiagram")));

    for (const MermaidBlock &b : m_doc.mermaid) {
        QCOMPARE(b.key.size(), 40);   // sha1 hex
        QVERIFY(m_doc.html.contains(QStringLiteral("<img src=\"mermaid://%1\"").arg(b.key)));
    }
    // The mermaid source must not also survive as a code block
    QVERIFY(!m_doc.html.contains(QStringLiteral("sequenceDiagram")));
}

void TestE2eRegression::rawHtmlIsEscapedInsideCodeBlocks()
{
    QVERIFY2(m_doc.html.contains(QStringLiteral("&lt;script&gt;alert(1)&lt;/script&gt;")),
             "程式碼區塊裡的 <script> 沒被轉義");
    QVERIFY(!m_doc.html.contains(QStringLiteral("<script>")));
}

void TestE2eRegression::unsupportedTagsAreRewritten()
{
    QVERIFY2(m_doc.html.contains(QStringLiteral("<s>刪除線</s>")), "刪除線沒轉成 <s>");
    QVERIFY(!m_doc.html.contains(QStringLiteral("<del>")));
    QVERIFY2(!m_doc.html.contains(QStringLiteral("<input")), "殘留 checkbox input");
    QVERIFY(m_doc.html.contains(QString::fromUtf8("\xE2\x98\x91")));   // ☑
    QVERIFY(m_doc.html.contains(QString::fromUtf8("\xE2\x98\x90")));   // ☐
}

// ------------------------------------------------------- Presentation layer

void TestE2eRegression::headingBlocksSurviveQtHtmlParser()
{
    // Scroll synchronisation depends entirely on this holding
    QCOMPARE(headingBlockCount(), m_doc.toc.size());
}

void TestE2eRegression::tableSurvivesAsQTextTable()
{
    QCOMPARE(tableCount(), 1);

    QTextTable *table = nullptr;
    for (QTextFrame *child : browser()->document()->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(child))
            table = t;
    QVERIFY(table);
    QCOMPARE(table->columns(), 3);
    QCOMPARE(table->rows(), 4);   // 表頭 + 3 列
    QCOMPARE(table->cellAt(0, 0).firstCursorPosition().block().text().trimmed(),
             QStringLiteral("欄位"));
    QCOMPARE(table->cellAt(3, 0).firstCursorPosition().block().text().trimmed(),
             QStringLiteral("中文欄位"));
}

void TestE2eRegression::codeBlocksKeepMonospaceAndPreserveWhitespace()
{
    const QString plain = browser()->toPlainText();
    QVERIFY2(plain.contains(QStringLiteral("template <typename T>")), "C++ 區塊內容不見了");
    QVERIFY2(plain.contains(QStringLiteral("def slug(text: str) -> str:")), "Python 區塊不見了");

    // Indentation must survive; that is what <pre> means
    QVERIFY2(plain.contains(QStringLiteral("    return \"-\".join")), "程式碼縮排被吃掉");

    // Code fragments must carry a monospace family.
    // Note: Qt does not set fontFixedPitch from a CSS font-family — that is a
    // separate flag — so this checks the family, not the flag.
    int monoFragments = 0;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid())
                continue;
            const QStringList families = f.charFormat().fontFamilies().toStringList();
            if (families.contains(QStringLiteral("monospace"), Qt::CaseInsensitive))
                ++monoFragments;
        }
    }
    QVERIFY2(monoFragments > 0, "程式碼片段沒有套上 monospace 字族");
    qInfo() << "等寬字片段數 =" << monoFragments;
}

void TestE2eRegression::taskListGlyphsAppearInPlainText()
{
    const QString plain = browser()->toPlainText();
    QVERIFY2(plain.contains(QString::fromUtf8("\xE2\x98\x91")), "已完成的待辦沒顯示 ☑");
    QVERIFY2(plain.contains(QString::fromUtf8("\xE2\x98\x90")), "未完成的待辦沒顯示 ☐");
}

void TestE2eRegression::scriptTagShowsAsLiteralText()
{
    const QString plain = browser()->toPlainText();
    QVERIFY2(plain.contains(QStringLiteral("<script>alert(1)</script>")),
             "程式碼區塊裡的標籤沒有以字面顯示");
}

void TestE2eRegression::missingImageIsReplacedByLabel()
{
    QVERIFY2(browser()->toPlainText().contains(QStringLiteral("[缺少圖片: does-not-exist.png]")),
             qPrintable(browser()->toPlainText().right(400)));
}

void TestE2eRegression::everyImageFragmentHasPositiveSize()
{
    const QTextDocument *doc = browser()->document();
    int images = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || !f.charFormat().isImageFormat())
                continue;
            ++images;
            const QTextImageFormat fmt = f.charFormat().toImageFormat();
            QVERIFY2(fmt.width() > 0 && fmt.height() > 0,
                     qPrintable(QStringLiteral("圖片 %1 尺寸為 0").arg(fmt.name())));
            QVERIFY2(fmt.width() <= 980,
                     qPrintable(QStringLiteral("圖片 %1 超出內容欄寬: %2")
                                    .arg(fmt.name()).arg(fmt.width())));
        }
    }
    // Two mermaid diagrams; the missing image was replaced by text so it does
    // not count
    QCOMPARE(images, 2);
}

void TestE2eRegression::contentColumnIsCappedAndCentred()
{
    // In a 1400 px window the content column should be capped near 980 and
    // centred, via viewport margins
    QVERIFY(m_win->width() >= 1300);
    const int vw = browser()->viewport()->width();
    qInfo() << "視窗寬" << m_win->width() << " 內容視埠寬" << vw;
    QVERIFY2(vw <= 990, qPrintable(QStringLiteral("內容欄沒被限制寬度: %1").arg(vw)));
}

// ------------------------------------------ Invariants across state changes

void TestE2eRegression::themeRoundTripPreservesStructure()
{
    const int headings = headingBlockCount();
    const int tables = tableCount();

    QAction *black = actionNamed(QStringLiteral("黑色主題"));
    QAction *white = actionNamed(QStringLiteral("白色主題"));
    QVERIFY(black && white);

    black->trigger();
    QCOMPARE(headingBlockCount(), headings);
    QCOMPARE(tableCount(), tables);
    QVERIFY(browser()->toPlainText().contains(QStringLiteral("中文欄位")));

    white->trigger();
    QCOMPARE(headingBlockCount(), headings);
    QCOMPARE(tableCount(), tables);
}

void TestE2eRegression::reloadPreservesStructure()
{
    const int headings = headingBlockCount();
    const int tables = tableCount();

    QVERIFY(m_win->openFile(QString::fromUtf8(SAMPLE_MD)));

    QCOMPARE(headingBlockCount(), headings);
    QCOMPARE(tableCount(), tables);
    QCOMPARE(m_win->findChild<QTreeWidget *>()->topLevelItemCount(), 1);
}

void TestE2eRegression::inlineCodeIsVisuallyDistinct()
{
    // Inline `code` must have its own colour and background so it is instantly
    // distinguishable from body text.
    for (Theme::Mode m : { Theme::Light, Theme::Dark }) {
        actionNamed(Theme::name(m))->trigger();

        const QColor pageBg(Theme::colors(m).background);
        const QColor bodyText(Theme::colors(m).text);

        bool found = false;
        const QTextDocument *doc = browser()->document();
        for (QTextBlock b = doc->begin(); b.isValid() && !found; b = b.next()) {
            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment f = it.fragment();
                if (!f.isValid() || !f.text().contains(QStringLiteral("inline code")))
                    continue;
                found = true;
                const QTextCharFormat cf = f.charFormat();

                const QStringList fams = cf.fontFamilies().toStringList();
                QVERIFY2(fams.contains(QStringLiteral("monospace"), Qt::CaseInsensitive),
                         qPrintable(QStringLiteral("行內 code 不是等寬字: %1")
                                        .arg(fams.join(QLatin1Char(',')))));

                QVERIFY2(cf.background().style() != Qt::NoBrush,
                         "行內 code 沒有底色");
                const QColor bg = cf.background().color();
                QVERIFY2(Theme::contrastRatio(bg, pageBg) > 1.05,
                         qPrintable(QStringLiteral("行內 code 底色與頁面底色幾乎一樣: %1 vs %2")
                                        .arg(bg.name(), pageBg.name())));

                QVERIFY2(cf.foreground().style() != Qt::NoBrush, "行內 code 沒有自己的前景色");
                const QColor fg = cf.foreground().color();
                QVERIFY2(fg != bodyText,
                         qPrintable(QStringLiteral("行內 code 的顏色與正文相同: %1").arg(fg.name())));

                // Assert the *exact* theme colours. The earlier version only
                // checked "different from body text", and still passed when a
                // CSS placeholder shift gave the background the foreground
                // colour — because the contrast fixup then rewrote the
                // foreground to something readable. Loose assertions hide bugs.
                QCOMPARE(fg, QColor(Theme::colors(m).codeInline));
                QCOMPARE(bg, QColor(Theme::colors(m).codeInlineBackground));
                QVERIFY2(Theme::contrastRatio(fg, bg) >= Theme::MinTextContrast,
                         qPrintable(QStringLiteral("行內 code 對比不足: %1 on %2 = %3:1")
                                        .arg(fg.name(), bg.name())
                                        .arg(Theme::contrastRatio(fg, bg))));
                qInfo().noquote() << Theme::name(m) << "行內 code: fg" << fg.name()
                                  << "bg" << bg.name();
            }
        }
        QVERIFY2(found, "找不到行內 code 片段");
    }
    actionNamed(Theme::name(Theme::Light))->trigger();
}

void TestE2eRegression::blockquoteMarkerContractHolds()
{
    // Qt rich text has no block-level border, so the blockquote bar is painted
    // by hand and "which blocks are blockquotes" is decided by
    // leftMargin == Theme::BlockquoteIndentPx. This pins that contract: the
    // stylesheet and the painting code share one constant.
    int quoteBlocks = 0;
    int headingBlocks = 0;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        if (bf.headingLevel() > 0) {
            ++headingBlocks;
            QCOMPARE(bf.leftMargin(), qreal(0));   // 標題不能被誤判成引用
            continue;
        }
        if (b.textList())
            continue;                              // 清單項目由 textList 排除
        if (qFuzzyCompare(bf.leftMargin() + 1.0, qreal(Theme::BlockquoteIndentPx) + 1.0))
            ++quoteBlocks;
    }

    QCOMPARE(headingBlocks, m_doc.toc.size());
    QCOMPARE(quoteBlocks, 2);   // sample.md 的引用區塊有兩段
}

void TestE2eRegression::tableUsesHorizontalRulesOnly()
{
    // Tables use horizontal rules only, matching the Chrome extension's look.
    // This relies on Qt's native borderCollapse plus per-cell borders rather
    // than custom painting — without borderCollapse(true) Qt draws no cell
    // borders at all.
    QTextTable *table = nullptr;
    for (QTextFrame *child : browser()->document()->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(child))
            table = t;
    QVERIFY(table);

    const QTextTableFormat tf = table->format();
    QCOMPARE(tf.border(), qreal(0));            // 整體外框不畫
    QVERIFY2(tf.borderCollapse(), "沒開 borderCollapse，cell 邊框不會被渲染");
    QCOMPARE(tf.cellPadding(), qreal(8));
    QCOMPARE(tf.cellSpacing(), qreal(0));

    const int rows = table->rows();
    QVERIFY(rows >= 2);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < table->columns(); ++c) {
            const QTextTableCellFormat cf = table->cellAt(r, c).format().toTableCellFormat();
            QVERIFY2(qFuzzyIsNull(cf.leftBorder()),
                     qPrintable(QStringLiteral("(%1,%2) 有左框線").arg(r).arg(c)));
            QVERIFY2(qFuzzyIsNull(cf.rightBorder()),
                     qPrintable(QStringLiteral("(%1,%2) 有右框線").arg(r).arg(c)));

            if (r == 0) {
                QCOMPARE(cf.topBorder(), qreal(0));      // 表頭上方不畫
                QVERIFY2(cf.bottomBorder() >= 2.0, "表頭下方的分隔線不夠明顯");
            } else {
                QCOMPARE(cf.topBorder(), qreal(1));
            }
        }
    }

    // The last row needs a bottom border to close the table off
    const QTextTableCellFormat lastCell =
        table->cellAt(rows - 1, 0).format().toTableCellFormat();
    QVERIFY(lastCell.bottomBorder() >= 1.0);
}

void TestE2eRegression::headingSizesFollowThemeScale()
{
    // Guards a Qt trap: while QTextFormat::FontSizeAdjustment is present — even
    // set to 0 — Qt ignores FontPointSize entirely and uses
    // "default size x level factor". Measured, H1 set to 23pt drew at 18pt and
    // H5 set to 11.5pt drew at 7.2pt, smaller than body text.
    // The fix is clearProperty() in applyHeadingScale(); setting it to 0 does
    // nothing.
    QVERIFY2(m_win->openFile(QString::fromUtf8(HEADINGS_MD)), HEADINGS_MD);

    QMap<int, qreal> seen;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const int level = b.blockFormat().headingLevel();
        if (level < 1 || level > 6)
            continue;
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || f.charFormat().isImageFormat())
                continue;

            const QTextCharFormat cf = f.charFormat();
            const qreal want = Theme::headingPointSize(level);

            QVERIFY2(!cf.hasProperty(QTextFormat::FontSizeAdjustment),
                     qPrintable(QStringLiteral("H%1 還留著 FontSizeAdjustment，"
                                               "字級會被 Qt 蓋掉").arg(level)));
            QVERIFY2(qAbs(cf.font().pointSizeF() - want) < 0.01,
                     qPrintable(QStringLiteral("H%1 實際字級 %2pt，預期 %3pt（片段: %4）")
                                    .arg(level).arg(cf.font().pointSizeF()).arg(want)
                                    .arg(f.text().left(12))));
            QVERIFY2(cf.font().bold(),
                     qPrintable(QStringLiteral("H%1 不是粗體").arg(level)));
            seen.insert(level, cf.font().pointSizeF());
        }
    }

    // headings.md must really cover all six levels, or this verifies nothing
    for (int level = 1; level <= 6; ++level)
        QVERIFY2(seen.contains(level),
                 qPrintable(QStringLiteral("語料裡沒有 H%1").arg(level)));

    // The scale must decrease strictly, and the deepest level must not be
    // smaller than body text
    for (int level = 1; level < 6; ++level)
        QVERIFY2(seen.value(level) > seen.value(level + 1),
                 qPrintable(QStringLiteral("H%1 (%2pt) 沒有大於 H%3 (%4pt)")
                                .arg(level).arg(seen.value(level))
                                .arg(level + 1).arg(seen.value(level + 1))));
    QVERIFY2(seen.value(6) >= Theme::BodyPointSize,
             qPrintable(QStringLiteral("H6 (%1pt) 比正文 (%2pt) 還小")
                            .arg(seen.value(6)).arg(Theme::BodyPointSize)));

    QVERIFY(m_win->openFile(QString::fromUtf8(SAMPLE_MD)));
}

void TestE2eRegression::bodyTextUsesComfortableLineHeight()
{
    // Qt's default is roughly single spacing, which is cramped for CJK text.
    // The Chrome extension uses 1.5 (24px on a 16px font); this uses 155%.
    // It also confirms Qt honours CSS line-height at all — not every property
    // in Qt's rich-text subset does.
    int checked = 0;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.text().trimmed().isEmpty() || b.blockFormat().headingLevel() > 0)
            continue;
        const QTextBlockFormat bf = b.blockFormat();
        if (qFuzzyIsNull(bf.lineHeight()) && bf.lineHeightType() == QTextBlockFormat::SingleHeight)
            continue;   // 表格內的空 cell 之類

        QCOMPARE(bf.lineHeightType(), int(QTextBlockFormat::ProportionalHeight));
        QCOMPARE(bf.lineHeight(), Theme::LineHeightPercent);
        ++checked;
    }
    QVERIFY2(checked >= 10,
             qPrintable(QStringLiteral("只有 %1 個 block 套到行高，語料可能有問題")
                            .arg(checked)));
    qInfo() << "套用行高的 block 數 =" << checked;
}

void TestE2eRegression::wideTableOpensQuickly()
{
    // A regression that actually happened: a 6.9 KB, 72-line file containing a
    // 65-row table took 2146 ms to open while sample.md took 9 ms. Each document
    // tree walk triggered a full re-layout per cell or fragment, which is
    // quadratic in the cell count. The fix was to wrap each walk's mutations in
    // one edit block and disable the undo stack, taking it to 23 ms — 93x.
    // This test keeps that fix from being undone.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString md = QStringLiteral("# 大表格\n\n| 頁數 | Space | key | 最舊 | 最新 |\n"
                                "| ---: | --- | --- | --- | --- |\n");
    const int rows = 300;
    for (int i = 0; i < rows; ++i)
        md += QStringLiteral("| %1 | [Space %1](spaces/s%1/_INDEX.md) | `K%1` | 2025-01-01 | 2026-08-31 |\n")
                  .arg(i);

    const QString path = dir.path() + QStringLiteral("/wide.md");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(md.toUtf8());
    f.close();

    m_win->openFile(path);          // 暖身（字型快取等）

    QElapsedTimer timer;
    timer.start();
    QVERIFY(m_win->openFile(path));
    const qint64 ms = timer.elapsed();

    // Confirm the corpus really produced that many cells, or this verifies
    // nothing
    int cells = 0;
    for (QTextFrame *fr : browser()->document()->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(fr))
            cells += t->rows() * t->columns();
    QVERIFY2(cells >= 1500, qPrintable(QStringLiteral("只有 %1 個 cell").arg(cells)));

    qInfo() << "寬表格" << cells << "個 cell 開檔耗時" << ms << "ms";

    // The 1500 ms bound is a sentinel against going quadratic again, not a
    // performance benchmark. Before the fix, 325 cells alone took 2146 ms, and
    // this corpus has 4.6 times as many.
    QVERIFY2(ms < 1500,
             qPrintable(QStringLiteral("%1 個 cell 花了 %2ms，可能又退回逐項重排")
                            .arg(cells).arg(ms)));

    QVERIFY(m_win->openFile(QString::fromUtf8(SAMPLE_MD)));
}

/// Actual point size of the fragment containing a given text (-1 if absent)
static qreal fragmentPointSize(const QTextDocument *doc, const QString &needle)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.isValid() && f.text().contains(needle))
                return f.charFormat().font().pointSizeF();
        }
    return -1;
}

void TestE2eRegression::zoomScalesBodyAndHeadingsTogether()
{
    // The request was Ctrl+/Ctrl- to zoom the font. Measurement found two
    // problems with the original implementation:
    //  1. headings did not scale at all — applyHeadingScale() hard-coded the
    //     point size
    //  2. body text was really the widget's system default (9pt); the
    //     stylesheet's `body { font-size }` had no effect whatsoever
    // Zoom is now an explicit factor plus setDefaultFont, so both scale.
    const QTextDocument *doc = browser()->document();

    const qreal bodyBefore = fragmentPointSize(doc, QStringLiteral("這份文件刻意"));
    const qreal h1Before = fragmentPointSize(doc, QStringLiteral("驗證文件"));
    QVERIFY(bodyBefore > 0 && h1Before > 0);
    QCOMPARE(bodyBefore, Theme::BodyPointSize);
    QCOMPARE(h1Before, Theme::headingPointSize(1));

    actionNamed(QStringLiteral("放大"))->trigger();
    actionNamed(QStringLiteral("放大"))->trigger();

    const qreal bodyAfter = fragmentPointSize(browser()->document(),
                                              QStringLiteral("這份文件刻意"));
    const qreal h1After = fragmentPointSize(browser()->document(),
                                            QStringLiteral("驗證文件"));
    qInfo() << "內文" << bodyBefore << "→" << bodyAfter
            << " H1" << h1Before << "→" << h1After;

    QVERIFY2(bodyAfter > bodyBefore, "內文沒有放大");
    QVERIFY2(h1After > h1Before, "標題沒有放大 —— 字級被寫死了");

    // The point: they must scale together, or the proportions break when zoomed
    const double bodyRatio = bodyAfter / bodyBefore;
    const double h1Ratio = h1After / h1Before;
    QVERIFY2(qAbs(bodyRatio - h1Ratio) < 0.02,
             qPrintable(QStringLiteral("內文放大 %1 倍但標題放大 %2 倍")
                            .arg(bodyRatio).arg(h1Ratio)));

    actionNamed(QStringLiteral("原始大小"))->trigger();
    QCOMPARE(fragmentPointSize(browser()->document(), QStringLiteral("這份文件刻意")),
             bodyBefore);
    QCOMPARE(fragmentPointSize(browser()->document(), QStringLiteral("驗證文件")),
             h1Before);
}

void TestE2eRegression::zoomIsClampedAndResettable()
{
    QAction *zin = actionNamed(QStringLiteral("放大"));
    QAction *zout = actionNamed(QStringLiteral("縮小"));
    QVERIFY(zin && zout);

    for (int i = 0; i < 40; ++i)
        zin->trigger();
    const qreal maxBody = fragmentPointSize(browser()->document(),
                                            QStringLiteral("這份文件刻意"));
    QVERIFY2(maxBody <= Theme::BodyPointSize * TextBrowserBackend::MaxZoom + 0.01,
             qPrintable(QStringLiteral("放大沒有上限: %1pt").arg(maxBody)));

    for (int i = 0; i < 80; ++i)
        zout->trigger();
    const qreal minBody = fragmentPointSize(browser()->document(),
                                            QStringLiteral("這份文件刻意"));
    QVERIFY2(minBody >= Theme::BodyPointSize * TextBrowserBackend::MinZoom - 0.01,
             qPrintable(QStringLiteral("縮小沒有下限: %1pt").arg(minBody)));
    QVERIFY2(minBody > 3.0, "縮到看不見了");

    actionNamed(QStringLiteral("原始大小"))->trigger();
    QCOMPARE(fragmentPointSize(browser()->document(), QStringLiteral("這份文件刻意")),
             Theme::BodyPointSize);
}

void TestE2eRegression::zoomShortcutsIncludeCtrlEquals()
{
    // What the user actually hit: QKeySequence::ZoomIn is Ctrl++ on Linux, and
    // '+' needs Ctrl+Shift+= on most keyboards, so the natural Ctrl+= did
    // nothing.
    const QList<QKeySequence> in = actionNamed(QStringLiteral("放大"))->shortcuts();
    QVERIFY2(in.contains(QKeySequence(Qt::CTRL | Qt::Key_Equal)),
             "放大沒有綁 Ctrl+= —— 一般鍵盤按不到 Ctrl++");
    QVERIFY(in.contains(QKeySequence::ZoomIn));

    const QList<QKeySequence> out = actionNamed(QStringLiteral("縮小"))->shortcuts();
    QVERIFY(out.contains(QKeySequence(Qt::CTRL | Qt::Key_Minus)));

    QCOMPARE(actionNamed(QStringLiteral("原始大小"))->shortcut(),
             QKeySequence(Qt::CTRL | Qt::Key_0));
}

void TestE2eRegression::linksAreUnderlinedAndUseLinkColour()
{
    bool found = false;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid() && !found; b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || !f.charFormat().isAnchor())
                continue;
            if (!f.text().contains(QStringLiteral("外部連結")))
                continue;
            found = true;
            const QTextCharFormat cf = f.charFormat();
            QVERIFY2(cf.fontUnderline(), "連結沒有底線");
            QCOMPARE(cf.foreground().color(), QColor(Theme::colors(Theme::Light).link));
        }
    }
    QVERIFY2(found, "找不到外部連結片段");
}

void TestE2eRegression::everyTextFragmentIsReadableInBothThemes()
{
    // The overall "nothing may be invisible" invariant: sample.md deliberately
    // contains raw HTML with hard-coded colours, so if the backend's contrast
    // fixup does not run — or misses a path — this fails.
    for (Theme::Mode m : { Theme::Light, Theme::Dark }) {
        actionNamed(Theme::name(m))->trigger();
        QCOMPARE(browser()->palette().color(QPalette::Base),
                 QColor(Theme::colors(m).background));

        const QStringList bad = lowContrastFragments(m);
        if (!bad.isEmpty())
            qWarning().noquote() << Theme::name(m) << "對比不足的片段:\n"
                                 << bad.join(QStringLiteral("\n"));
        QVERIFY2(bad.isEmpty(),
                 qPrintable(QStringLiteral("%1 有 %2 個看不見／難看見的片段")
                                .arg(Theme::name(m)).arg(bad.size())));
    }
    actionNamed(Theme::name(Theme::Light))->trigger();
}

void TestE2eRegression::hardcodedColoursInSampleAreCorrected()
{
    // Confirms the previous test did not pass vacuously with nothing to fix
    actionNamed(Theme::name(Theme::Dark))->trigger();

    int checked = 0;
    const QTextDocument *doc = browser()->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid())
                continue;
            if (!f.text().contains(QStringLiteral("寫死純黑"))
                && !f.text().contains(QStringLiteral("寫死白底")))
                continue;

            const QTextCharFormat cf = f.charFormat();
            const QColor fg = cf.foreground().style() != Qt::NoBrush
                                  ? cf.foreground().color()
                                  : QColor(Theme::colors(Theme::Dark).text);
            QColor bg(Theme::colors(Theme::Dark).background);
            if (cf.background().style() != Qt::NoBrush && cf.background().color().alpha() > 0)
                bg = cf.background().color();

            const double r = Theme::contrastRatio(fg, bg);
            qInfo().noquote() << f.text().left(20) << "→ fg" << fg.name()
                              << "bg" << bg.name() << QStringLiteral("%1:1").arg(r, 0, 'f', 2);
            QVERIFY(r >= Theme::MinTextContrast);
            ++checked;
        }
    }
    QVERIFY2(checked >= 2,
             qPrintable(QStringLiteral("只找到 %1 個寫死顏色的片段，語料可能被改掉了")
                            .arg(checked)));
    actionNamed(Theme::name(Theme::Light))->trigger();
}

void TestE2eRegression::dumpScreenshotsIfRequested()
{
    // With MD_E2E_DUMP=<dir> set, save key screens as PNGs for a human to look
    // at. Automated assertions verify structure but not whether it *looks*
    // right; this covers that gap.
    const QString dir = qEnvironmentVariable("MD_E2E_DUMP");
    if (dir.isEmpty())
        QSKIP("未設定 MD_E2E_DUMP，跳過視覺輸出");

    QVERIFY(QDir().mkpath(dir));

    // MD_E2E_DOC and MD_E2E_ANCHORS aim the capture at a different document and
    // sections, which makes checking one specific aspect (heading scale, say)
    // reproducible.
    const QString altDoc = qEnvironmentVariable("MD_E2E_DOC");
    const QString altAnchors = qEnvironmentVariable("MD_E2E_ANCHORS");
    if (!altDoc.isEmpty()) {
        QVERIFY2(m_win->openFile(altDoc), qPrintable(altDoc));

        const QStringList anchors = altAnchors.isEmpty()
                                        ? QStringList{ QString() }
                                        : altAnchors.split(QLatin1Char(','));
        QAction *white = actionNamed(Theme::name(Theme::Light));
        QAction *black = actionNamed(Theme::name(Theme::Dark));
        int n = 1;
        for (const bool dark : { false, true }) {
            (dark ? black : white)->trigger();
            for (const QString &anchor : anchors) {
                if (anchor.isEmpty())
                    browser()->verticalScrollBar()->setValue(0);
                else
                    browser()->scrollToAnchor(anchor.trimmed());
                QTest::qWait(150);
                const QString path = QStringLiteral("%1/%2-%3-%4.png")
                                         .arg(dir)
                                         .arg(n++, 2, 10, QLatin1Char('0'))
                                         .arg(dark ? QStringLiteral("black")
                                                   : QStringLiteral("white"),
                                              anchor.isEmpty() ? QStringLiteral("top")
                                                               : anchor.trimmed());
                QVERIFY2(m_win->grab().save(path), qPrintable(path));
                qInfo() << "已存" << path;
            }
        }
        white->trigger();
        QVERIFY(m_win->openFile(QString::fromUtf8(SAMPLE_MD)));
        return;
    }

    struct Shot {
        QString name;
        QString anchor;   // 空字串表示文件頂端
        bool dark;
    };
    const QList<Shot> shots{
        { QStringLiteral("01-top-white"),      QString(),                false },
        { QStringLiteral("02-code-white"),     QStringLiteral("程式碼"),  false },
        { QStringLiteral("03-mermaid-white"),  QStringLiteral("mermaid"), false },
        { QStringLiteral("04-table-white"),    QStringLiteral("表格"),     false },
        { QStringLiteral("05-contrast-white"), QStringLiteral("對比保護"), false },
        { QStringLiteral("06-top-black"),      QString(),                true  },
        { QStringLiteral("07-code-black"),     QStringLiteral("程式碼"),  true  },
        { QStringLiteral("08-mermaid-black"),  QStringLiteral("mermaid"), true  },
        { QStringLiteral("09-table-black"),    QStringLiteral("表格"),     true  },
        { QStringLiteral("10-contrast-black"), QStringLiteral("對比保護"), true  },
    };

    for (const Shot &sh : shots) {
        actionNamed(Theme::name(sh.dark ? Theme::Dark : Theme::Light))->trigger();

        // Wait for the diagrams to finish, or the shot only catches placeholders
        QTRY_VERIFY_WITH_TIMEOUT([this] {
            const QTextDocument *doc = browser()->document();
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
                for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                    const QTextFragment f = it.fragment();
                    if (f.isValid() && f.charFormat().isImageFormat()
                        && int(f.charFormat().toImageFormat().height()) == 88)
                        return false;
                }
            return true;
        }(), 120000);

        if (sh.anchor.isEmpty())
            browser()->verticalScrollBar()->setValue(0);
        else
            browser()->scrollToAnchor(sh.anchor);
        QTest::qWait(120);

        const QString path = QStringLiteral("%1/%2.png").arg(dir, sh.name);
        QVERIFY2(m_win->grab().save(path), qPrintable(path));
        qInfo() << "已存" << path;
    }

    actionNamed(Theme::name(Theme::Light))->trigger();

    // Zoom levels: check headings and body scale together without breaking the
    // layout
    QAction *zin = actionNamed(QStringLiteral("放大"));
    QAction *zreset = actionNamed(QStringLiteral("原始大小"));
    for (const int steps : { 0, 3, 6 }) {
        zreset->trigger();
        for (int i = 0; i < steps; ++i)
            zin->trigger();
        browser()->verticalScrollBar()->setValue(0);
        QTest::qWait(120);
        const QString path = QStringLiteral("%1/zoom-%2.png").arg(dir).arg(steps, 2, 10,
                                                                        QLatin1Char('0'));
        QVERIFY2(m_win->grab().save(path), qPrintable(path));
        qInfo() << "已存" << path;
    }
    zreset->trigger();
}

QTEST_MAIN(TestE2eRegression)
#include "test_e2e_regression.moc"
