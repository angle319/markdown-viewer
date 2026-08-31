#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QSvgRenderer>
#include <QTemporaryDir>

#include "core/MermaidCache.h"
#include "core/MmdcRenderer.h"

/// 會真的執行外部 mmdc 的整合測試。mmdc 不在就整支 skip，不當失敗。
///
/// 這支測試的核心任務是把「為什麼預設輸出是 PNG 而不是 SVG」釘成可執行的證據。
/// 規劃階段留下的風險是「Qt 的 QSvgRenderer 只支援 SVG Tiny 1.2」，實測結論是
/// 它撐不住 mermaid：即使關掉 htmlLabels 讓文字變成真正的 <text>，Qt 仍然不支援
/// <marker>，結果是**所有連線與箭頭整批消失**。
/// svgOutputLosesEdgesInQt() 就是在盯住這件事，別讓人日後「順手」改回 SVG。
class TestMmdcIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void rendererIsDiscoverable();
    void defaultOutputIsPngAtDoubleScale();
    void pngScaleAffectsPixelSize();
    void pngDrawsEdgesBetweenNodes();
    void svgOutputLosesEdgesInQt();
    void svgOutputHasNoForeignObject();
    void cjkLabelsProduceInkInPng();
    void secondRequestHitsCacheWithoutRerender();
    void darkThemeProducesDifferentOutput();

private:
    QString renderSync(MermaidCache *cache, const QString &source, bool dark,
                       int timeoutMs = 60000);

    /// 影像正中央水平帶的深色墨水量。
    /// 對 `A[...] --> B[...]` 這種左右佈局，中央帶就是連線與箭頭所在之處：
    /// 兩個節點方框分別落在左右兩側，中間的空隙只可能被連線佔用。
    static qint64 inkInCenterBand(const QImage &img);
    static QImage loadAsImage(const QString &path);
    static qint64 totalInk(const QImage &img);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<MmdcRenderer> m_renderer;
    QScopedPointer<MermaidCache> m_cache;

    /// 左右佈局、節點文字夠長，確保中央有明顯的連線區段
    static QString twoNodeDiagram()
    {
        return QStringLiteral("flowchart LR\n  A[AAAAAAAA] --> B[BBBBBBBB]\n");
    }
};

void TestMmdcIntegration::initTestCase()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());
    m_renderer.reset(new MmdcRenderer);
    m_cache.reset(new MermaidCache(m_renderer.data()));
    m_cache->setCacheDir(m_dir->path() + QStringLiteral("/mermaid"));

    if (!m_renderer->isAvailable())
        QSKIP("找不到 mmdc，跳過整合測試（npm i -g @mermaid-js/mermaid-cli）");
}

QString TestMmdcIntegration::renderSync(MermaidCache *cache, const QString &source, bool dark,
                                        int timeoutMs)
{
    if (cache->isCached(source, dark))
        return cache->pathFor(source, dark);

    QSignalSpy done(cache, &MermaidCache::rendered);
    QSignalSpy failed(cache, &MermaidCache::failed);
    cache->request(source, dark);

    QElapsedTimer timer;
    timer.start();
    while (done.isEmpty() && failed.isEmpty() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    if (!failed.isEmpty()) {
        qWarning() << "mmdc 失敗:" << failed.at(0).at(1).toString();
        return {};
    }
    return done.isEmpty() ? QString() : cache->pathFor(source, dark);
}

QImage TestMmdcIntegration::loadAsImage(const QString &path)
{
    if (path.endsWith(QLatin1String(".svg"))) {
        QSvgRenderer r(path);
        if (!r.isValid())
            return {};
        QSize sz = r.defaultSize();
        if (!sz.isValid() || sz.isEmpty())
            sz = QSize(800, 300);
        QImage img(sz, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        r.render(&p);
        return img;
    }
    return QImage(path);
}

qint64 TestMmdcIntegration::totalInk(const QImage &img)
{
    qint64 ink = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 24)
                ++ink;
    return ink;
}

qint64 TestMmdcIntegration::inkInCenterBand(const QImage &img)
{
    if (img.isNull())
        return -1;

    const int x0 = int(img.width() * 0.45);
    const int x1 = int(img.width() * 0.55);
    const int y0 = int(img.height() * 0.42);
    const int y1 = int(img.height() * 0.58);

    qint64 ink = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const QRgb px = img.pixel(x, y);
            if (qAlpha(px) < 40)
                continue;
            if (qGray(px) < 200)   // 連線是深色的
                ++ink;
        }
    }
    return ink;
}

void TestMmdcIntegration::rendererIsDiscoverable()
{
    QVERIFY(!MmdcRenderer::findMmdc().isEmpty());
    QVERIFY(m_renderer->isAvailable());
    QVERIFY2(m_renderer->rendererId().startsWith(QStringLiteral("mmdc-")),
             qPrintable(m_renderer->rendererId()));
    QVERIFY2(!m_renderer->rendererId().endsWith(QStringLiteral("unknown")),
             "取不到 mmdc 版本，快取 key 會失去工具版本的區辨力");
}

void TestMmdcIntegration::defaultOutputIsPngAtDoubleScale()
{
    QCOMPARE(m_renderer->outputExtension(), QStringLiteral("png"));
    QCOMPARE(m_renderer->outputScale(), qreal(2.0));
    QCOMPARE(m_cache->outputScale(), qreal(2.0));
    QVERIFY(m_cache->pathFor(twoNodeDiagram(), false).endsWith(QStringLiteral(".png")));
}

void TestMmdcIntegration::pngScaleAffectsPixelSize()
{
    MmdcRenderer r1;
    r1.setPngScale(1);
    MermaidCache c1(&r1);
    c1.setCacheDir(m_dir->path() + QStringLiteral("/scale1"));

    const QString p1 = renderSync(&c1, twoNodeDiagram(), false);
    const QString p2 = renderSync(m_cache.data(), twoNodeDiagram(), false);
    QVERIFY(!p1.isEmpty());
    QVERIFY(!p2.isEmpty());

    const QImage i1 = loadAsImage(p1);
    const QImage i2 = loadAsImage(p2);
    QVERIFY(!i1.isNull());
    QVERIFY(!i2.isNull());

    const double ratio = double(i2.width()) / double(i1.width());
    qInfo() << "1x =" << i1.size() << " 2x =" << i2.size() << " ratio =" << ratio;
    QVERIFY2(qAbs(ratio - 2.0) < 0.15,
             qPrintable(QStringLiteral("2x 的像素寬度不是 1x 的兩倍: %1").arg(ratio)));
}

void TestMmdcIntegration::pngDrawsEdgesBetweenNodes()
{
    const QString path = renderSync(m_cache.data(), twoNodeDiagram(), false);
    QVERIFY(!path.isEmpty());

    const QImage img = loadAsImage(path);
    QVERIFY(!img.isNull());

    const qint64 band = inkInCenterBand(img);
    qInfo() << "PNG 中央帶墨水 =" << band << " 影像尺寸 =" << img.size();
    QVERIFY2(band > 20, qPrintable(QStringLiteral(
        "兩個節點之間沒有連線像素（band=%1）—— 箭頭遺失").arg(band)));
}

void TestMmdcIntegration::svgOutputLosesEdgesInQt()
{
    // 這支測試刻意記錄一個已知缺陷：Qt SVG Tiny 不支援 <marker>，
    // 導致 mermaid 的連線與箭頭在 Qt 裡整批消失。
    // 若哪天 Qt 修好了，這支會失敗 —— 那正是重新考慮 SVG 的時機。
    MmdcRenderer svgRenderer;
    svgRenderer.setOutputExtension(QStringLiteral("svg"));
    MermaidCache svgCache(&svgRenderer);
    svgCache.setCacheDir(m_dir->path() + QStringLiteral("/assvg"));

    const QString svgPath = renderSync(&svgCache, twoNodeDiagram(), false);
    QVERIFY(!svgPath.isEmpty());
    QVERIFY(svgPath.endsWith(QStringLiteral(".svg")));

    const QImage viaQt = loadAsImage(svgPath);
    QVERIFY2(!viaQt.isNull(), "QSvgRenderer 連解析都失敗");

    const qint64 svgBand = inkInCenterBand(viaQt);
    const QString pngPath = renderSync(m_cache.data(), twoNodeDiagram(), false);
    const qint64 pngBand = inkInCenterBand(loadAsImage(pngPath));

    qInfo() << "中央帶墨水  SVG(經 Qt) =" << svgBand << " PNG(經 Chromium) =" << pngBand;

    QVERIFY2(pngBand > svgBand * 3,
             qPrintable(QStringLiteral("SVG 竟然畫出了連線（svg=%1 png=%2）—— "
                                       "Qt 可能已支援 marker，可重新評估 SVG 模式")
                            .arg(svgBand).arg(pngBand)));
    // 圖形本身（方框、文字）還是有畫出來的，缺的是連線
    QVERIFY2(totalInk(viaQt) > 0, "SVG 完全空白，與預期的『只缺連線』不符");
}

void TestMmdcIntegration::svgOutputHasNoForeignObject()
{
    // htmlLabels:false 的效果驗證：沒有 foreignObject，文字是真正的 <text>。
    // 這是 SVG 模式的必要（但不充分）條件。
    MmdcRenderer svgRenderer;
    svgRenderer.setOutputExtension(QStringLiteral("svg"));
    MermaidCache svgCache(&svgRenderer);
    svgCache.setCacheDir(m_dir->path() + QStringLiteral("/assvg2"));

    const QString src = QStringLiteral("flowchart LR\n  A[讀取檔案] --> B[顯示結果]\n");
    const QString path = renderSync(&svgCache, src, false);
    QVERIFY(!path.isEmpty());

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray svg = f.readAll();

    QVERIFY2(!svg.contains("foreignObject"), "產出含 foreignObject，Qt 會讓文字全空白");
    QVERIFY2(svg.contains("<text"), "沒有 <text> 元素");
    QVERIFY2(svg.contains(QStringLiteral("讀取檔案").toUtf8()), "CJK 標籤沒進 SVG");
    QVERIFY2(!svg.left(400).contains("width=\"100%\""), "width=\"100%\" 沒被移除");
    QVERIFY(svg.left(400).contains("viewBox"));
}

void TestMmdcIntegration::cjkLabelsProduceInkInPng()
{
    // 差分法：同結構的圖，一次帶中文標籤、一次標籤留空。
    // 帶文字的單位面積墨水必須明顯較多，才算文字真的被畫出來。
    const QString withText = QStringLiteral("flowchart LR\n  A[讀取檔案內容] --> B[顯示結果畫面]\n");
    const QString blank = QStringLiteral("flowchart LR\n  A[ ] --> B[ ]\n");

    const QImage i1 = loadAsImage(renderSync(m_cache.data(), withText, false));
    const QImage i2 = loadAsImage(renderSync(m_cache.data(), blank, false));
    QVERIFY(!i1.isNull());
    QVERIFY(!i2.isNull());

    const double d1 = double(totalInk(i1)) / double(i1.width() * i1.height());
    const double d2 = double(totalInk(i2)) / double(i2.width() * i2.height());
    qInfo() << "墨水密度 帶中文 =" << d1 << " 空標籤 =" << d2;

    QVERIFY2(d1 > d2 * 1.15,
             qPrintable(QStringLiteral("中文標籤似乎沒被畫出來: %1 vs %2").arg(d1).arg(d2)));
}

void TestMmdcIntegration::secondRequestHitsCacheWithoutRerender()
{
    const QString src = QStringLiteral("flowchart LR\n  cache --> hit\n");
    const QString path = renderSync(m_cache.data(), src, false);
    QVERIFY(!path.isEmpty());
    QVERIFY(m_cache->isCached(src, false));

    const QDateTime firstMtime = QFileInfo(path).lastModified();

    QSignalSpy rendered(m_cache.data(), &MermaidCache::rendered);
    m_cache->request(src, false);
    QCOMPARE(m_cache->pendingCount(), 0);
    QCoreApplication::processEvents();
    QCOMPARE(rendered.count(), 0);
    QCOMPARE(QFileInfo(path).lastModified(), firstMtime);
}

void TestMmdcIntegration::darkThemeProducesDifferentOutput()
{
    const QString src = QStringLiteral("flowchart LR\n  theme --> test\n");
    const QString light = renderSync(m_cache.data(), src, false);
    const QString dark = renderSync(m_cache.data(), src, true);
    QVERIFY(!light.isEmpty());
    QVERIFY(!dark.isEmpty());
    QVERIFY2(light != dark, "明暗主題共用同一個快取檔");

    QFile f1(light), f2(dark);
    QVERIFY(f1.open(QIODevice::ReadOnly));
    QVERIFY(f2.open(QIODevice::ReadOnly));
    QVERIFY2(f1.readAll() != f2.readAll(), "明暗主題產出內容相同");
}

QTEST_MAIN(TestMmdcIntegration)
#include "test_mmdc_integration.moc"
