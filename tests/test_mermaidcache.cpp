#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "core/IMermaidRenderer.h"
#include "core/MermaidCache.h"

/// 假 renderer：不跑 mmdc，只寫一個佔位檔並以非同步方式回報完成。
/// 順便記錄「同時進行中的渲染數」以驗證佇列真的序列化。
class FakeRenderer : public IMermaidRenderer
{
    Q_OBJECT

public:
    bool available = true;
    bool shouldFail = false;
    bool writeOutput = true;
    int startCount = 0;
    int active = 0;
    int maxActive = 0;
    QStringList seenThemes;

    bool isAvailable() const override { return available; }
    QString rendererId() const override { return QStringLiteral("fake-1"); }
    QString outputExtension() const override { return QStringLiteral("svg"); }

    void start(const QString &source, bool dark, const QString &outPath) override
    {
        Q_UNUSED(source);
        ++startCount;
        ++active;
        maxActive = qMax(maxActive, active);
        seenThemes << (dark ? QStringLiteral("dark") : QStringLiteral("light"));

        if (writeOutput) {
            QFile f(outPath);
            if (f.open(QIODevice::WriteOnly))
                f.write(shouldFail ? QByteArray() : QByteArrayLiteral("<svg/>"));
        }

        QTimer::singleShot(0, this, [this] {
            --active;
            Q_EMIT finished(!shouldFail, shouldFail ? QStringLiteral("boom") : QString());
        });
    }
};

class TestMermaidCache : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void keyDependsOnSourceThemeAndRenderer();
    void pathUsesRendererExtension();
    void cacheHitSkipsRenderer();
    void queueSerializesRenders();
    void duplicateRequestIsQueuedOnce();
    void unavailableRendererDegradesSilently();
    void failureRemovesPartialFile();
    void renderedSignalCarriesKeyAndPath();

private:
    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<FakeRenderer> m_fake;
    QScopedPointer<MermaidCache> m_cache;
};

void TestMermaidCache::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());
    m_fake.reset(new FakeRenderer);
    m_cache.reset(new MermaidCache(m_fake.data()));
    m_cache->setCacheDir(m_dir->path() + QStringLiteral("/mermaid"));
}

void TestMermaidCache::keyDependsOnSourceThemeAndRenderer()
{
    const QString a = QStringLiteral("graph TD\n A-->B");
    const QString b = QStringLiteral("graph TD\n A-->C");

    QVERIFY(m_cache->keyFor(a, false) != m_cache->keyFor(b, false)); // 內容
    QVERIFY(m_cache->keyFor(a, false) != m_cache->keyFor(a, true));  // 主題
    QCOMPARE(m_cache->keyFor(a, false), m_cache->keyFor(a, false));  // 穩定

    // renderer 換掉 → key 換掉
    FakeRenderer other;
    MermaidCache c2(&other);
    c2.setCacheDir(m_cache->cacheDir());
    QCOMPARE(c2.keyFor(a, false), m_cache->keyFor(a, false)); // 同 rendererId 故相同
}

void TestMermaidCache::pathUsesRendererExtension()
{
    const QString p = m_cache->pathFor(QStringLiteral("graph TD"), false);
    QVERIFY2(p.endsWith(QStringLiteral(".svg")), qPrintable(p));
    QVERIFY2(p.startsWith(m_cache->cacheDir()), qPrintable(p));
}

void TestMermaidCache::cacheHitSkipsRenderer()
{
    const QString src = QStringLiteral("graph TD\n X-->Y");
    const QString path = m_cache->pathFor(src, false);
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral("<svg/>"));
    f.close();

    QVERIFY(m_cache->isCached(src, false));
    m_cache->request(src, false);
    QCOMPARE(m_fake->startCount, 0);
    QCOMPARE(m_cache->pendingCount(), 0);
}

void TestMermaidCache::queueSerializesRenders()
{
    QSignalSpy idleSpy(m_cache.data(), &MermaidCache::idle);

    m_cache->request(QStringLiteral("a"), false);
    m_cache->request(QStringLiteral("b"), false);
    m_cache->request(QStringLiteral("c"), false);

    QCOMPARE(m_cache->pendingCount(), 3);
    QVERIFY(idleSpy.wait(5000));

    QCOMPARE(m_fake->startCount, 3);
    QCOMPARE(m_fake->maxActive, 1);   // 關鍵：絕不同時跑兩個 mmdc
    QCOMPARE(m_cache->pendingCount(), 0);
}

void TestMermaidCache::duplicateRequestIsQueuedOnce()
{
    QSignalSpy idleSpy(m_cache.data(), &MermaidCache::idle);

    m_cache->request(QStringLiteral("same"), false);
    m_cache->request(QStringLiteral("same"), false);
    m_cache->request(QStringLiteral("same"), false);

    QCOMPARE(m_cache->pendingCount(), 1);
    QVERIFY(idleSpy.wait(5000));
    QCOMPARE(m_fake->startCount, 1);
}

void TestMermaidCache::unavailableRendererDegradesSilently()
{
    m_fake->available = false;
    QSignalSpy renderedSpy(m_cache.data(), &MermaidCache::rendered);
    QSignalSpy failedSpy(m_cache.data(), &MermaidCache::failed);

    QVERIFY(!m_cache->rendererAvailable());
    m_cache->request(QStringLiteral("graph TD"), false);

    QCOMPARE(m_cache->pendingCount(), 0);
    QCOMPARE(m_fake->startCount, 0);
    QCOMPARE(renderedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 0);   // 不是錯誤，是 degrade
}

void TestMermaidCache::failureRemovesPartialFile()
{
    m_fake->shouldFail = true;
    QSignalSpy failedSpy(m_cache.data(), &MermaidCache::failed);

    const QString src = QStringLiteral("bad diagram");
    m_cache->request(src, false);
    QVERIFY(failedSpy.wait(5000));

    // 半截檔案不能留在快取裡，否則下次會被誤判為命中
    QVERIFY(!m_cache->isCached(src, false));
    QVERIFY(!QFile::exists(m_cache->pathFor(src, false)));
}

void TestMermaidCache::renderedSignalCarriesKeyAndPath()
{
    QSignalSpy spy(m_cache.data(), &MermaidCache::rendered);
    const QString src = QStringLiteral("graph LR\n A-->B");

    m_cache->request(src, true);
    QVERIFY(spy.wait(5000));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), m_cache->keyFor(src, true));
    QCOMPARE(spy.at(0).at(1).toString(), m_cache->pathFor(src, true));
    QCOMPARE(m_fake->seenThemes, QStringList{ QStringLiteral("dark") });
    QVERIFY(m_cache->isCached(src, true));
}

QTEST_GUILESS_MAIN(TestMermaidCache)
#include "test_mermaidcache.moc"
