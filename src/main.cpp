#include "panel.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("input-source-panel");
    app.setApplicationVersion("0.1.2");
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DDE 显示器输入源插件调试面板"));
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"demo", "Use sample displays without hardware access"});
    parser.addOption({"screenshot", "Save a panel screenshot and exit", "path"});
    parser.addOption({"quit-after", "Exit after a given number of milliseconds", "milliseconds"});
    parser.process(app);
    Controller controller(parser.isSet("demo"));
    Panel panel(&controller);
    panel.show();
    if (parser.isSet("screenshot")) {
        const QString path = parser.value("screenshot");
        QTimer::singleShot(400, &app, [&panel, &app, path] {
            app.exit(panel.grab().save(path) ? 0 : 1);
        });
    }
    if (parser.isSet("quit-after")) {
        bool ok = false;
        const int delay = parser.value("quit-after").toInt(&ok);
        if (!ok || delay < 1) { QTextStream(stderr) << "Invalid --quit-after value\n"; return 2; }
        QTimer::singleShot(delay, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
