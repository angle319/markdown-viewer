#include "MainWindow.h"

#include "DocumentArea.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QSettings>

int main(int argc, char *argv[])
{
    // This application paints entirely through the raster engine and never uses
    // OpenGL. Letting the xcb QPA load its GL integration drags in Mesa's
    // llvmpipe and libLLVM, which measured 13 MB of PSS on its own (49.1 MB
    // baseline down to 32.7 MB without it). An explicit user setting wins.
    if (!qEnvironmentVariableIsSet("QT_XCB_GL_INTEGRATION"))
        qputenv("QT_XCB_GL_INTEGRATION", "none");

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("markdown-tool"));
    QApplication::setOrganizationName(QStringLiteral("markdown-tool"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("極簡 markdown 檢視器（md4c + QTextBrowser，無瀏覽器引擎）"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("要開啟的 markdown 檔"));
    parser.process(app);

    MainWindow w;
    w.show();

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        // Several files may be given; each opens in its own tab
        for (const QString &a : args)
            w.openFile(a);
    } else {
        // With no arguments, restore the tabs from the previous session
        QSettings settings;
        const QStringList tabs = settings.value(QStringLiteral("files/openTabs")).toStringList();
        for (const QString &p : tabs)
            if (QFileInfo(p).isFile())
                w.openFile(p);

        if (tabs.isEmpty()) {
            const QString last = settings.value(QStringLiteral("files/lastFile")).toString();
            if (!last.isEmpty() && QFileInfo(last).isFile())
                w.openFile(last);
        }
        w.area()->setActiveIndex(settings.value(QStringLiteral("files/activeTab"), 0).toInt());
    }

    return app.exec();
}
