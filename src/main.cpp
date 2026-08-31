#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QSettings>

int main(int argc, char *argv[])
{
    // 這個 app 全程是 raster 繪製，完全不用 OpenGL。若放任 xcb QPA 載入它的
    // GL 整合，會把 Mesa 的 llvmpipe 連帶 libLLVM 拉進行程 —— 實測光那一顆
    // 就佔 13MB PSS（基準 49.1MB → 32.7MB）。使用者若明確設過就尊重其設定。
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
        w.openFile(args.first());
    } else {
        // 沒給參數就接續上次的檔案
        const QString last = QSettings().value(QStringLiteral("files/lastFile")).toString();
        if (!last.isEmpty() && QFileInfo(last).isFile())
            w.openFile(last);
    }

    return app.exec();
}
