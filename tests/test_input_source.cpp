#include "controller.h"
#include "panel.h"
#include <pluginsiteminterface.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPluginLoader>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class FakeProxy : public PluginProxyInterface {
public:
    int added = 0, removed = 0;
    QMap<QString, QVariant> values;
    void itemAdded(PluginsItemInterface *const, const QString &) override { ++added; }
    void itemUpdate(PluginsItemInterface *const, const QString &) override {}
    void itemRemoved(PluginsItemInterface *const, const QString &) override { ++removed; }
    void requestWindowAutoHide(PluginsItemInterface *const, const QString &, bool) override {}
    void requestRefreshWindowVisible(PluginsItemInterface *const, const QString &) override {}
    void requestSetAppletVisible(PluginsItemInterface *const, const QString &, bool) override {}
    void saveValue(PluginsItemInterface *const, const QString &key, const QVariant &value) override { values[key] = value; }
    const QVariant getValue(PluginsItemInterface *const, const QString &key, const QVariant &fallback) override { return values.value(key, fallback); }
    void removeValue(PluginsItemInterface *const, const QStringList &) override {}
};
class TestInputSource : public QObject {
    Q_OBJECT
private slots:
    void sourceNames() {
        QCOMPARE(inputName(0x11), QStringLiteral("HDMI 1"));
        QCOMPARE(inputName(0x0f), QStringLiteral("DisplayPort 1"));
        QVERIFY(inputName(0x1b).contains("0X1B"));
        QVERIFY(!inputName(0x1b).contains("USB"));
        QCOMPARE(inputName(-1), QStringLiteral("未知"));
    }
    void connectorNames() {
        QCOMPARE(connectorName("DisplayPort-2"), QStringLiteral("DP-2"));
        QCOMPARE(connectorName("card0-DP-2"), QStringLiteral("DP-2"));
        QCOMPARE(connectorName("HDMI-A-1"), QStringLiteral("HDMI-1"));
    }
    void matching() {
        Monitor a, b;
        a.edid = b.edid = QString(256, 'a'); a.bus = 3; b.bus = 7;
        QCOMPARE(matchingMonitor({a, b}, 7, a.edid), 1);
        QCOMPARE(matchingMonitor({a, b}, 9, a.edid), -1);
        QCOMPARE(matchingMonitor({a, b}, 3, QString(256, 'b')), -1);
        a.bus = b.bus = -1;
        QCOMPARE(matchingMonitor({a, b}, 3, a.edid), -1);
        QCOMPARE(matchingMonitor({a}, 3, a.edid), 0);
        QCOMPARE(matchingMonitor({a}, 3, QString()), -1);
    }
    void sourceValidation() {
        Monitor m; m.bus = 4; m.edid = QString(256, 'a'); m.sources = {15, 17}; m.controllable = true;
        QVERIFY(allowedSource(m, 17, false));
        QVERIFY(!allowedSource(m, 18, false));
        QVERIFY(allowedSource(m, 18, true));
        QVERIFY(!allowedSource(m, 256, true));
        QVERIFY(!allowedSource(m, 0, true));
        QVERIFY(!validTarget(-1, m.edid, 17));
        QVERIFY(!validTarget(4, QString(256, 'z'), 17));
        m.controllable = false;
        QVERIFY(!allowedSource(m, 17, true));
    }
    void jsonRoundTrip() {
        Monitor m; m.id = "card1-HDMI-A-1"; m.name = "示例屏幕"; m.connector = "HDMI-1";
        m.bus = 7; m.edid = QString(256, 'a'); m.current = 17; m.sources = {15, 17, 18}; m.controllable = true;
        QCOMPARE(Monitor::fromJson(m.toJson()).toJson(), m.toJson());
        auto object = m.toJson(); object["sources"] = QJsonArray{0, -1, 17, 17, 300};
        QCOMPARE(Monitor::fromJson(object).sources, QVector<int>{17});
    }
    void drmEnumeration() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(data) == data.size();
        };
        const QString external = temp.path() + "/card0-HDMI-A-1";
        const QString builtin = temp.path() + "/card0-eDP-1";
        const QString disconnected = temp.path() + "/card0-DP-2";
        QVERIFY(QDir().mkpath(external)); QVERIFY(QDir().mkpath(builtin)); QVERIFY(QDir().mkpath(disconnected));
        QVERIFY(write(external + "/status", "connected\n"));
        QVERIFY(write(builtin + "/status", "connected\n"));
        QVERIFY(write(disconnected + "/status", "disconnected\n"));
        QByteArray edid(128, '\0');
        edid[57] = char(0xfc); edid.replace(59, 13, QByteArray("TEST MONITOR\n ", 13));
        QVERIFY(write(external + "/edid", edid));
        const QString bus = temp.path() + "/i2c-8"; QVERIFY(QDir().mkpath(bus));
        QVERIFY(QFile::link(bus, external + "/ddc"));
        const auto monitors = drmMonitors(temp.path());
        QCOMPARE(monitors.size(), 2);
        QCOMPARE(monitors[0].connector, QStringLiteral("HDMI-1"));
        QCOMPARE(monitors[0].name, QStringLiteral("TEST MONITOR"));
        QCOMPARE(monitors[0].bus, 8);
        QCOMPARE(monitors[0].edid.size(), 256);
        QVERIFY(!monitors[1].controllable);
        QVERIFY(monitors[1].error.contains(QStringLiteral("内置")));
        QVERIFY(edidText(QByteArray(12, 'a'), 0xfc).isEmpty());
    }
    void displayPortAuxSelection() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString connector = temp.path() + "/card0-DP-2";
        QVERIFY(QDir().mkpath(connector + "/i2c-5"));
        const auto write = [](const QString &path, const QByteArray &data) {
            QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
        };
        QVERIFY(write(connector + "/status", "connected\n"));
        QVERIFY(write(connector + "/edid", QByteArray(128, 'a')));
        QVERIFY(write(connector + "/i2c-5/name", "AMDGPU DM aux hw bus 1\n"));
        QVERIFY(QDir().mkpath(temp.path() + "/i2c-2"));
        QVERIFY(QFile::link(temp.path() + "/i2c-2", connector + "/ddc"));
        auto monitors = drmMonitors(temp.path());
        QCOMPARE(monitors.size(), 1);
        QCOMPARE(monitors[0].bus, 5);
        QCOMPARE(matchingMonitor(monitors, 5, QString::fromLatin1(QByteArray(128, 'a').toHex())), 0);
        QCOMPARE(matchingMonitor(monitors, 2, monitors[0].edid), -1);
        // Ambiguous AUX children cannot override a known physical association.
        QVERIFY(QDir().mkpath(connector + "/i2c-6"));
        QVERIFY(write(connector + "/i2c-6/name", "AMDGPU DM aux hw bus 2\n"));
        monitors = drmMonitors(temp.path());
        QCOMPARE(monitors[0].bus, 2);
    }
    void demoInteraction() {
        Controller controller(true);
        Panel panel(&controller); panel.show();
        QCOMPARE(controller.monitors().size(), 3);
        QSignalSpy messages(&controller, &Controller::message);
        auto combos = panel.findChildren<QComboBox *>(); QCOMPARE(combos.size(), 2);
        auto *combo = combos.first(); combo->setCurrentIndex(combo->findData(17));
        auto buttons = panel.findChildren<QPushButton *>();
        QPushButton *switchButton = nullptr;
        for (auto *button : buttons) if (button->text() == QStringLiteral("切换") && button->isEnabled()) { switchButton = button; break; }
        QVERIFY(switchButton); QTest::mouseClick(switchButton, Qt::LeftButton);
        QCOMPARE(controller.monitors()[0].current, 17);
        QVERIFY(messages.count() > 0);
        QCOMPARE(controller.monitors()[1].current, 17);
        controller.switchSource("demo-dp", 0x7f, false);
        QCOMPARE(controller.monitors()[0].current, 17);
        controller.switchSource("demo-dp", 0x7f, true);
        QCOMPARE(controller.monitors()[0].current, 0x7f);
        controller.switchSource("demo-internal", 17, true);
        QCOMPARE(controller.monitors()[2].current, -1);
    }
    void missingHelper() {
        const QByteArray previous = qgetenv("DDE_INPUT_SOURCE_HELPER");
        qputenv("DDE_INPUT_SOURCE_HELPER", "/nonexistent/input-source-helper");
        Controller controller;
        QSignalSpy spy(&controller, &Controller::message);
        controller.refresh();
        QTRY_VERIFY(!controller.busy());
        QVERIFY(!spy.isEmpty());
        QVERIFY(spy.last().at(1).toBool());
        QVERIFY(spy.last().at(0).toString().contains(QStringLiteral("无法启动")));
        if (previous.isNull()) qunsetenv("DDE_INPUT_SOURCE_HELPER");
        else qputenv("DDE_INPUT_SOURCE_HELPER", previous);
    }
    void malformedHelperResponse() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const QString path = temp.path() + "/helper";
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\necho malformed-response\n"); file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray previous = qgetenv("DDE_INPUT_SOURCE_HELPER");
        qputenv("DDE_INPUT_SOURCE_HELPER", path.toUtf8());
        Controller controller;
        QSignalSpy spy(&controller, &Controller::message);
        controller.refresh(); QTRY_VERIFY(!controller.busy());
        QVERIFY(spy.last().at(1).toBool());
        QVERIFY(spy.last().at(0).toString().contains(QStringLiteral("后台程序异常")));
        // The OS-visible screens remain listed if the backend fails.
        QVERIFY(!controller.monitors().isEmpty());
        if (previous.isNull()) qunsetenv("DDE_INPUT_SOURCE_HELPER");
        else qputenv("DDE_INPUT_SOURCE_HELPER", previous);
    }
    void helperRejectsInvalidTarget() {
        QTemporaryDir runtime; QVERIFY(runtime.isValid());
        QProcess helper;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("XDG_RUNTIME_DIR", runtime.path());
        helper.setProcessEnvironment(environment);
        helper.start(QCoreApplication::applicationDirPath() + "/dde-dock-input-source-helper",
                     {"set", "-1", "invalid-edid", "256"});
        QVERIFY(helper.waitForFinished(3000));
        QCOMPARE(helper.exitCode(), 1);
        const auto response = QJsonDocument::fromJson(helper.readAllStandardOutput()).object();
        QVERIFY(!response.value("ok").toBool());
        QVERIFY(response.value("error").toString().contains(QStringLiteral("无效")));
    }
    void pluginLifecycle() {
        FakeProxy proxy;
        QPluginLoader loader(QString::fromUtf8(TEST_PLUGIN_PATH));
        QObject *instance = loader.instance();
        QVERIFY2(instance, qPrintable(loader.errorString()));
        auto *plugin = qobject_cast<PluginsItemInterface *>(instance); QVERIFY(plugin);
        plugin->init(&proxy); QCOMPARE(proxy.added, 1);
        QVERIFY(plugin->itemWidget("input-source"));
        QVERIFY(plugin->itemTipsWidget("input-source"));
        QVERIFY(plugin->itemPopupApplet("input-source"));
        plugin->setSortKey("input-source", 5); QCOMPARE(plugin->itemSortKey("input-source"), 5);
        plugin->pluginStateSwitched(); QVERIFY(plugin->pluginIsDisable()); QCOMPARE(proxy.removed, 1);
        plugin->pluginStateSwitched(); QVERIFY(!plugin->pluginIsDisable()); QCOMPARE(proxy.added, 2);
        QVERIFY(loader.unload());
    }
};
QTEST_MAIN(TestInputSource)
#include "test_input_source.moc"
