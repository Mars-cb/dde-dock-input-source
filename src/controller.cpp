#include "controller.h"
#include "displaywatcher.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>

Controller::Controller(bool demo, QObject *parent) : QObject(parent), m_demo(demo)
{
    m_timeout.setSingleShot(true);
    m_hotplug.setSingleShot(true);
    connect(&m_hotplug, &QTimer::timeout, this, &Controller::refresh);
    // Explicit interval: switching uses a longer delay on this same timer.
    const auto scheduleRefresh = [this] { m_hotplug.start(700); };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, scheduleRefresh);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, scheduleRefresh);
    if (!m_demo) {
        auto *watcher = new DisplayWatcher(this);
        connect(watcher, &DisplayWatcher::changed, this, scheduleRefresh);
    }
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &Controller::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            complete(true, QStringLiteral("无法启动后台程序：%1").arg(m_process.errorString()));
    });
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        m_process.kill();
    });
}
Controller::~Controller()
{
    m_timeout.stop();
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}
void Controller::addDesktopScreens()
{
    for (QScreen *screen : QGuiApplication::screens()) {
        const QString connector = connectorName(screen->name());
        bool found = false;
        for (const auto &m : m_monitors) if (m.connector == connector) { found = true; break; }
        if (found) continue;
        Monitor m;
        m.id = "screen:" + screen->name(); m.name = screen->name(); m.connector = connector;
        m.error = QStringLiteral("此屏幕未匹配到 DDC/CI 接口（可能为内置屏、虚拟屏或未开放设备权限）");
        m_monitors.append(m);
    }
}
void Controller::initialize()
{
    if (!m_initialized) refresh();
}
void Controller::refresh()
{
    m_initialized = true;
    if (m_busy) { m_refreshPending = true; return; }
    if (m_demo) {
        m_monitors.clear();
        Monitor a;
        a.id = "demo-dp"; a.name = "DELL U2723QE"; a.connector = "DP-1";
        a.bus = 4; a.edid = QString(256, 'a'); a.current = 15;
        a.controllable = true; a.sources = {15, 17, 18};
        Monitor b = a;
        b.id = "demo-hdmi"; b.name = "LG ULTRAGEAR"; b.connector = "HDMI-1";
        b.bus = 6; b.edid = QString(256, 'b'); b.current = 17; b.sources = {15, 17, 18};
        Monitor c;
        c.id = "demo-internal"; c.name = "内置显示屏"; c.connector = "eDP-1";
        c.error = "内置屏幕不支持外部输入源切换";
        m_monitors = {a, b, c};
        emit changed();
        emit message(QStringLiteral("演示模式 · 切换仅更新示例，不会操作硬件"), false);
        return;
    }
    // Present the OS-visible screens immediately, even if DDC later times out.
    m_monitors = drmMonitors();
    addDesktopScreens();
    emit changed();
    start({"scan"}, false);
}
void Controller::switchSource(const QString &id, int value, bool manual)
{
    if (m_busy) return;
    for (auto &m : m_monitors) if (m.id == id) {
        if (!allowedSource(m, value, manual)) {
            emit message(QStringLiteral("此输入源不可用，请刷新显示器信息"), true);
            return;
        }
        if (m_demo) {
            m.current = value;
            emit changed();
            emit message(QStringLiteral("演示：已切换至 %1，未操作硬件").arg(inputName(value)), false);
            return;
        }
        QStringList args{"set", QString::number(m.bus), m.edid, QString::number(value)};
        if (manual) args.append("--manual");
        start(args, true);
        return;
    }
    emit message(QStringLiteral("显示器信息已变化，请刷新后重试"), true);
}
void Controller::start(const QStringList &args, bool switching)
{
    QString helper = qEnvironmentVariable("DDE_INPUT_SOURCE_HELPER");
    if (helper.isEmpty()) {
        const QString sibling = QCoreApplication::applicationDirPath() + "/dde-dock-input-source-helper";
        helper = QFileInfo::exists(sibling) ? sibling : QString::fromUtf8(INPUT_SOURCE_HELPER_PATH);
    }
    m_busy = true; m_switching = switching; m_timedOut = false;
    emit busyChanged(true);
    emit message(switching ? QStringLiteral("正在切换输入源…") : QStringLiteral("正在检测显示器与输入源…"), false);
    m_timeout.start(60000);
    m_process.start(helper, args);
}
void Controller::complete(bool error, const QString &text)
{
    m_timeout.stop();
    m_busy = false;
    emit busyChanged(false);
    emit message(text, error);
    if (m_refreshPending) {
        m_refreshPending = false;
        QTimer::singleShot(0, this, &Controller::refresh);
    }
}
void Controller::finish(int code, QProcess::ExitStatus exitStatus)
{
    if (!m_busy) return;
    const QByteArray output = m_process.readAllStandardOutput();
    const QString diagnostic = QString::fromLocal8Bit(m_process.readAllStandardError()).trimmed();
    if (m_timedOut) {
        complete(true, m_switching ? QStringLiteral("切换超时，显示器状态未知，请查看实际画面后刷新")
                                   : QStringLiteral("检测超时，请检查 DDC/CI 和 I²C 设备权限后刷新"));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(output, &parseError);
    if (exitStatus != QProcess::NormalExit || parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        complete(true, QStringLiteral("后台程序异常：%1").arg(diagnostic.isEmpty() ? QStringLiteral("未返回有效信息") : diagnostic.left(240)));
        return;
    }
    const auto obj = doc.object();
    if (code != 0 || !obj.value("ok").toBool()) {
        complete(true, obj.value("error").toString(QStringLiteral("操作失败，请刷新后重试")));
        return;
    }
    if (m_switching) {
        complete(false, QStringLiteral("切换指令已发送，请以显示器实际画面为准"));
        m_hotplug.start(2500);
        return;
    }
    if (!obj.value("monitors").isArray()) {
        complete(true, QStringLiteral("后台返回的显示器列表无效"));
        return;
    }
    m_monitors.clear();
    for (const auto &value : obj.value("monitors").toArray()) m_monitors.append(Monitor::fromJson(value.toObject()));
    addDesktopScreens();
    emit changed();
    QString notice = obj.value("notice").toString();
    complete(false, notice.isEmpty() ? QStringLiteral("已识别 %1 个显示器").arg(m_monitors.size()) : notice);
}
