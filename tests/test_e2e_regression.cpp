#include <QtTest>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QScrollBar>
#include <QTextFragment>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QTreeWidget>

#include "MainWindow.h"
#include "core/MarkdownParser.h"

#ifndef SAMPLE_MD
#  error "SAMPLE_MD 未定義（應由 CMake 提供 docs/sample.md 的路徑）"
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

    // --- 可選的視覺輸出 ---
    void dumpScreenshotsIfRequested();

private:
    QTextBrowser *browser() const { return m_win->findChild<QTextBrowser *>(); }
    int headingBlockCount() const;
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

    QAction *theme = nullptr;
    for (QAction *a : m_win->findChildren<QAction *>())
        if (a->text() == QStringLiteral("暗色主題"))
            theme = a;
    QVERIFY(theme);

    theme->setChecked(true);
    QCOMPARE(headingBlockCount(), headings);
    QCOMPARE(tableCount(), tables);
    QVERIFY(browser()->toPlainText().contains(QStringLiteral("中文欄位")));

    theme->setChecked(false);
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

void TestE2eRegression::dumpScreenshotsIfRequested()
{
    // 設 MD_E2E_DUMP=<目錄> 時，把幾個關鍵畫面存成 PNG 供人眼檢查。
    // 自動化斷言驗得了結構，驗不了「看起來對不對」——這條路徑補上那一塊。
    const QString dir = qEnvironmentVariable("MD_E2E_DUMP");
    if (dir.isEmpty())
        QSKIP("未設定 MD_E2E_DUMP，跳過視覺輸出");

    QVERIFY(QDir().mkpath(dir));

    struct Shot {
        QString name;
        QString anchor;   // 空字串表示文件頂端
        bool dark;
    };
    const QList<Shot> shots{
        { QStringLiteral("01-top-light"),     QString(),                       false },
        { QStringLiteral("02-code-light"),    QStringLiteral("程式碼"),         false },
        { QStringLiteral("03-mermaid-light"), QStringLiteral("mermaid"),        false },
        { QStringLiteral("04-table-light"),   QStringLiteral("表格"),           false },
        { QStringLiteral("05-top-dark"),      QString(),                       true  },
        { QStringLiteral("06-mermaid-dark"),  QStringLiteral("mermaid"),        true  },
    };

    QAction *theme = nullptr;
    for (QAction *a : m_win->findChildren<QAction *>())
        if (a->text() == QStringLiteral("暗色主題"))
            theme = a;
    QVERIFY(theme);

    for (const Shot &sh : shots) {
        theme->setChecked(sh.dark);

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

    theme->setChecked(false);
}

QTEST_MAIN(TestE2eRegression)
#include "test_e2e_regression.moc"
