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
#include "core/MarkdownParser.h"

#ifndef SAMPLE_MD
#  error "SAMPLE_MD 未定義（應由 CMake 提供 docs/sample.md 的路徑）"
#endif
#ifndef HEADINGS_MD
#  error "HEADINGS_MD 未定義（應由 CMake 提供 docs/headings.md 的路徑）"
#endif

/// 回歸測試：拿 docs/sample.md 當語法語料庫，把整條 pipeline 的不變式釘住。
///
/// 分成兩層：
///   1. 解析層 —— 直接檢查 MarkdownParser 的產出（錨點、mermaid、轉義…）
///   2. 呈現層 —— 檢查 QTextDocument 真的收下了那些結構（標題、表格、程式碼…）
///
/// 第 2 層存在的理由是第 1 層過不代表畫面對：Qt rich-text 只吃 HTML 的一個子集，
/// 「產出了正確的 HTML」與「Qt 保留了那個結構」是兩件事。
class TestE2eRegression : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // --- 解析層 ---
    void sampleFileExists();
    void tocAnchorsMatchGolden();
    void duplicateHeadingsGetGithubStyleSuffixes();
    void mermaidBlocksAreExtracted();
    void rawHtmlIsEscapedInsideCodeBlocks();
    void unsupportedTagsAreRewritten();

    // --- 呈現層 ---
    void headingBlocksSurviveQtHtmlParser();
    void tableSurvivesAsQTextTable();
    void codeBlocksKeepMonospaceAndPreserveWhitespace();
    void taskListGlyphsAppearInPlainText();
    void scriptTagShowsAsLiteralText();
    void missingImageIsReplacedByLabel();
    void everyImageFragmentHasPositiveSize();
    void contentColumnIsCappedAndCentred();

    // --- 狀態切換不破壞不變式 ---
    void themeRoundTripPreservesStructure();
    void reloadPreservesStructure();
    void inlineCodeIsVisuallyDistinct();
    void blockquoteMarkerContractHolds();
    void tableUsesHorizontalRulesOnly();
    void headingSizesFollowThemeScale();
    void bodyTextUsesComfortableLineHeight();
    void wideTableOpensQuickly();
    void linksAreUnderlinedAndUseLinkColour();
    void everyTextFragmentIsReadableInBothThemes();
    void hardcodedColoursInSampleAreCorrected();

    // --- 可選的視覺輸出 ---
    void dumpScreenshotsIfRequested();

private:
    QTextBrowser *browser() const
    {
        // 多分頁之後不能用 m_win->findChild —— 那會抓到第一個建立的 view，
        // 不一定是作用中的那個
        DocumentView *v = m_win->activeView();
        return v ? v->findChild<QTextBrowser *>() : nullptr;
    }
    QAction *actionNamed(const QString &text) const;
    int headingBlockCount() const;

    /// 掃全文件，回傳「對比不足的文字片段」清單（片段文字 + 前景 + 背景 + 比值）。
    /// 有效背景的判定順序與 render backend 一致：片段背景 → block 背景 → 頁面底色。
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

// ------------------------------------------------------------------ 解析層

void TestE2eRegression::sampleFileExists()
{
    QVERIFY(QFile::exists(QString::fromUtf8(SAMPLE_MD)));
    QVERIFY(m_markdown.contains(QStringLiteral("```mermaid")));
}

void TestE2eRegression::tocAnchorsMatchGolden()
{
    // 錨點是對外契約（複製貼上的連結會用到），變動必須是刻意的
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
    // mermaid 原始碼不該同時以程式碼區塊留在 HTML 裡
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

// ------------------------------------------------------------------ 呈現層

void TestE2eRegression::headingBlocksSurviveQtHtmlParser()
{
    // 捲動同步完全依賴這件事成立
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

    // 縮排必須保留（<pre> 的意義）
    QVERIFY2(plain.contains(QStringLiteral("    return \"-\".join")), "程式碼縮排被吃掉");

    // 程式碼片段必須套上等寬字族。
    // 註：Qt 不會因為 CSS 的 font-family 去設 fontFixedPitch —— 那是獨立旗標，
    // 所以這裡驗的是字型家族，而不是那個旗標。
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
    // 兩張 mermaid（缺少的那張圖已被換成文字，所以不算）
    QCOMPARE(images, 2);
}

void TestE2eRegression::contentColumnIsCappedAndCentred()
{
    // 視窗 1400 寬，內容欄應被限制在 980 附近並置中（靠 viewport margin）
    QVERIFY(m_win->width() >= 1300);
    const int vw = browser()->viewport()->width();
    qInfo() << "視窗寬" << m_win->width() << " 內容視埠寬" << vw;
    QVERIFY2(vw <= 990, qPrintable(QStringLiteral("內容欄沒被限制寬度: %1").arg(vw)));
}

// -------------------------------------------------------- 狀態切換不變式

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
    // 行內 `code` 必須有自己的顏色與底色，跟正文一眼分得出來。
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

                // 斷言「確切等於主題定義的顏色」。原本只驗「與正文不同」，
                // 結果 CSS placeholder 錯位（底色拿到前景色）時測試照樣通過 ——
                // 因為對比修正把前景改成了可讀的白色。寬鬆的斷言會掩蓋 bug。
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
    // Qt rich-text 沒有 block 層級的 border，所以引用區塊的左色條是自繪的，
    // 而「哪些 block 是引用區塊」靠 leftMargin == Theme::BlockquoteIndentPx 辨識。
    // 這支測試釘住那個契約：CSS 與繪製程式碼共用同一個常數。
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
    // 表格改成只有橫線（對照 Chrome extension 的觀感）。
    // 用 Qt 原生的 borderCollapse + 每個 cell 的邊框，不是自繪 ——
    // 少了 borderCollapse(true) 這行 Qt 根本不會畫 cell 層級的邊框。
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

    // 最後一列要有下框線把表格收尾
    const QTextTableCellFormat lastCell =
        table->cellAt(rows - 1, 0).format().toTableCellFormat();
    QVERIFY(lastCell.bottomBorder() >= 1.0);
}

void TestE2eRegression::headingSizesFollowThemeScale()
{
    // 這支測試盯住一個 Qt 的陷阱：QTextFormat::FontSizeAdjustment 只要存在
    // （即使值是 0），Qt 就完全忽略 FontPointSize，改用「預設字級 × 層級係數」。
    // 實測 H1 設 23pt 卻畫成 18pt、H5 設 11.5pt 卻畫成 7.2pt —— 比正文還小。
    // 修法是在 applyHeadingScale() 裡 clearProperty() 掉它（設成 0 沒有用）。
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

    // headings.md 必須真的涵蓋六個層級，否則這支測試等於沒驗
    for (int level = 1; level <= 6; ++level)
        QVERIFY2(seen.contains(level),
                 qPrintable(QStringLiteral("語料裡沒有 H%1").arg(level)));

    // 階層必須是嚴格遞減的，而且最深的一層不能小於正文
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
    // Qt 預設的行距約等於單行，中文在那個行距下很擠。對照 Chrome extension 是
    // 1.5（16px 字對 24px 行高），這裡用 155%。
    // 順帶確認 Qt 真的吃 CSS 的 line-height —— 它不是 Qt rich-text 一定支援的屬性。
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
    // 曾經的退化：一份 6.9KB、只有 72 行但含 65 列表格的文件要花 2146ms 開，
    // 而 sample.md 只要 9ms。原因是 document tree walk 每改一個 cell / 片段
    // 就觸發一次重新排版，在 cell 數多時是二次方級的成本。
    // 修法是把每個 walk 的修改包進單一 editBlock，並關掉 undo stack。
    // 修完是 23ms（93 倍）。這支測試守住那個修法別被改掉。
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

    // 確認語料真的產生了那麼多 cell，否則這支測試等於沒驗
    int cells = 0;
    for (QTextFrame *fr : browser()->document()->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(fr))
            cells += t->rows() * t->columns();
    QVERIFY2(cells >= 1500, qPrintable(QStringLiteral("只有 %1 個 cell").arg(cells)));

    qInfo() << "寬表格" << cells << "個 cell 開檔耗時" << ms << "ms";

    // 門檻放寬到 1500ms：這是「別再退回二次方」的哨兵，不是效能基準。
    // 退化前光 325 個 cell 就要 2146ms，這裡的 cell 數是它的 4.6 倍。
    QVERIFY2(ms < 1500,
             qPrintable(QStringLiteral("%1 個 cell 花了 %2ms，可能又退回逐項重排")
                            .arg(cells).arg(ms)));

    QVERIFY(m_win->openFile(QString::fromUtf8(SAMPLE_MD)));
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
    // 這是「不要有看不見的狀況」的總體不變式：sample.md 刻意包含寫死顏色的
    // 原始 HTML，若 render backend 的對比修正沒跑（或漏跑某條路徑），這裡會炸。
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
    // 確認上一支測試不是因為「沒有東西需要修正」而空過
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
    // 設 MD_E2E_DUMP=<目錄> 時，把幾個關鍵畫面存成 PNG 供人眼檢查。
    // 自動化斷言驗得了結構，驗不了「看起來對不對」——這條路徑補上那一塊。
    const QString dir = qEnvironmentVariable("MD_E2E_DUMP");
    if (dir.isEmpty())
        QSKIP("未設定 MD_E2E_DUMP，跳過視覺輸出");

    QVERIFY(QDir().mkpath(dir));

    // MD_E2E_DOC / MD_E2E_ANCHORS 可以改拍別的文件與別的段落，
    // 方便針對特定樣式（例如標題層級）重現檢查。
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

        // 等 mermaid 圖真的畫完再截，否則只會拍到「產生中」的佔位圖
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
}

QTEST_MAIN(TestE2eRegression)
#include "test_e2e_regression.moc"
