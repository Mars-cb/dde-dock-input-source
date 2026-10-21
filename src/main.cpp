#include "panel.h"
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCommandLineParser>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("input-source-panel");
    app.setApplicationVersion("0.1.3");
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DDE 显示器输入源插件调试面板"));
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"demo", "Use sample displays without hardware access"});
    parser.addOption({"screenshot", "Save a panel screenshot and exit", "path"});
    parser.addOption({"popup-screenshot", "Save the expanded input-source popup and exit", "path"});
    parser.addOption({"quit-after", "Exit after a given number of milliseconds", "milliseconds"});
    parser.process(app);
    Controller controller(parser.isSet("demo"));
    Panel panel(&controller);
    panel.show();
    if (parser.isSet("screenshot") || parser.isSet("popup-screenshot")) {
        const bool popup = parser.isSet("popup-screenshot");
        const QString path = parser.value(popup ? "popup-screenshot" : "screenshot");
        QTimer::singleShot(400, &app, [&panel, &app, path, popup] {
            if (!popup) { app.exit(panel.grab().save(path) ? 0 : 1); return; }
            const auto combos = panel.findChildren<QComboBox *>();
            if (combos.isEmpty()) { app.exit(2); return; }
            QComboBox *combo = combos.first();
            combo->showPopup();
            QTimer::singleShot(200, &app, [combo, &app, path] {
                const bool saved = combo->view()->window()->grab().save(path);
                combo->hidePopup();
                app.exit(saved ? 0 : 1);
            });
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
