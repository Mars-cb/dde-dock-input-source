#include "model.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QStandardPaths>
#include <QTextStream>
#include <ddcutil_c_api.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <memory>

static QString statusText(DDCA_Status status)
{
    if (status == -EACCES || status == -EPERM)
        return QStringLiteral("无权访问 I²C 设备，请检查 i2c 用户组或系统 udev 规则");
    return QStringLiteral("%1 (%2)").arg(QString::fromLocal8Bit(ddca_rc_desc(status))).arg(status);
}
struct Handle {
    DDCA_Display_Handle value = nullptr;
    ~Handle() { if (value) ddca_close_display(value); }
};
using DisplayList = std::unique_ptr<DDCA_Display_Info_List, decltype(&ddca_free_display_info_list)>;
static QString edidOf(const DDCA_Display_Info &info)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(info.edid_bytes), 128).toHex());
}
static void readInput(DDCA_Display_Handle handle, Monitor &m)
{
    DDCA_Non_Table_Vcp_Value value{};
    const auto currentStatus = ddca_get_non_table_vcp_value(handle, 0x60, &value);
    if (currentStatus == 0) m.current = (int(value.sh) << 8) | value.sl;
    char *text = nullptr;
    DDCA_Capabilities *caps = nullptr;
    const auto capsStatus = ddca_get_capabilities_string(handle, &text);
    if (capsStatus == 0 && text && ddca_parse_capabilities_string(text, &caps) == 0 && caps) {
        for (int i = 0; i < caps->vcp_code_ct; ++i) {
            const auto &feature = caps->vcp_codes[i];
            if (feature.feature_code != 0x60) continue;
            for (int j = 0; j < feature.value_ct; ++j) {
                const int n = feature.values[j];
                if (n > 0 && !m.sources.contains(n)) m.sources.append(n);
            }
        }
    }
    if (caps) ddca_free_parsed_capabilities(caps);
    std::free(text);
    m.controllable = currentStatus == 0 || !m.sources.isEmpty();
    if (!m.controllable)
        m.error = QStringLiteral("无法读取输入源：%1。请检查显示器是否启用 DDC/CI").arg(statusText(currentStatus));
    else if (m.sources.isEmpty())
        m.error = QStringLiteral("显示器未报告输入源列表，可使用手动模式");
    else if (currentStatus != 0)
        m.error = QStringLiteral("当前输入源读取失败：%1").arg(statusText(currentStatus));
    else m.error.clear();
}
static QJsonObject scan()
{
    QVector<Monitor> monitors = drmMonitors();
    const auto devices = QDir("/dev").entryList({"i2c-*"}, QDir::System | QDir::Files);
    QString notice;
    DDCA_Display_Info_List *raw = nullptr;
    DDCA_Status status = 0;
    if (devices.isEmpty()) {
        notice = QStringLiteral("未找到 /dev/i2c-*，请加载 i2c-dev 内核模块");
    } else {
        status = ddca_get_display_info_list2(true, &raw);
        if (status != 0) notice = statusText(status);
    }
    DisplayList list(raw, ddca_free_display_info_list);
    if (list) for (int i = 0; i < list->ct; ++i) {
        const auto &info = list->info[i];
        if (info.path.io_mode != DDCA_IO_I2C) continue;
        const int bus = info.path.path.i2c_busno;
        const QString edid = edidOf(info);
        int index = matchingMonitor(monitors, bus, edid);
        if (index < 0) {
            Monitor m;
            m.id = QStringLiteral("i2c-%1").arg(bus);
            m.name = QString::fromLocal8Bit(info.model_name);
            if (m.name.isEmpty()) m.name = QStringLiteral("显示器（I²C %1）").arg(bus);
            monitors.append(m);
            index = monitors.size() - 1;
        }
        Monitor &m = monitors[index];
        m.bus = bus; m.edid = edid;
        if (m.serial.isEmpty()) m.serial = QString::fromLocal8Bit(info.sn);
        Handle handle;
        status = ddca_open_display2(info.dref, false, &handle.value);
        if (status == 0) readInput(handle.value, m);
        else m.error = QStringLiteral("DDC/CI 不可用：%1").arg(statusText(status));
    }
    QJsonArray array;
    for (const auto &m : monitors) array.append(m.toJson());
    return {{"ok", true}, {"monitors", array}, {"notice", notice}};
}
static QJsonObject fail(const QString &error) { return {{"ok", false}, {"error", error}}; }
static QJsonObject switchInput(const QStringList &args)
{
    if (args.size() != 5 && args.size() != 6) return fail(QStringLiteral("用法：set BUS EDID VALUE [--manual]"));
    bool busOK = false, valueOK = false;
    const int bus = args[2].toInt(&busOK);
    const QString edid = args[3].toLower();
    const int value = args[4].toInt(&valueOK, 0);
    const bool manual = args.size() == 6 && args[5] == "--manual";
    if (!busOK || !valueOK || !validTarget(bus, edid, value) || (args.size() == 6 && !manual))
        return fail(QStringLiteral("无效的总线、EDID 或输入源编号"));
    DDCA_Display_Info_List *raw = nullptr;
    const auto detection = ddca_get_display_info_list2(false, &raw);
    DisplayList list(raw, ddca_free_display_info_list);
    if (detection != 0 || !list) return fail(QStringLiteral("重新检测显示器失败"));
    const DDCA_Display_Info *target = nullptr;
    for (int i = 0; i < list->ct; ++i) {
        const auto &info = list->info[i];
        if (info.path.io_mode == DDCA_IO_I2C && info.path.path.i2c_busno == bus && edidOf(info) == edid) {
            if (target) return fail(QStringLiteral("显示器标识不唯一，请刷新后重试"));
            target = &info;
        }
    }
    if (!target) return fail(QStringLiteral("显示器已断开或连接发生变化，请刷新后重试"));
    Handle handle;
    auto status = ddca_open_display2(target->dref, false, &handle.value);
    if (status != 0) return fail(statusText(status));
    Monitor monitor;
    monitor.bus = bus; monitor.edid = edid;
    readInput(handle.value, monitor);
    if (!allowedSource(monitor, value, manual))
        return fail(QStringLiteral("显示器未报告此输入源；请刷新或显式开启手动模式"));
    // This is the only hardware write in the project. No retries: a successful
    // switch can remove the current DDC link before any verification is possible.
    status = ddca_set_non_table_vcp_value(handle.value, 0x60, 0, static_cast<uint8_t>(value));
    if (status != 0) return fail(statusText(status));
    return {{"ok", true}, {"commandSent", true}, {"value", value}};
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("dde-dock-input-source-helper");
    const auto args = app.arguments();
    if (args.size() < 2 || (args[1] != "scan" && args[1] != "set")) {
        QTextStream(stdout) << QJsonDocument(fail(QStringLiteral("用法：scan | set BUS EDID VALUE [--manual]"))).toJson(QJsonDocument::Compact) << '\n';
        return 2;
    }
    // Keep stdout reserved for a single JSON response, including on old libddcutil.
    ddca_set_fout(stderr);
    ddca_set_ferr(stderr);
    ddca_set_output_level(DDCA_OL_TERSE);
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QLockFile lock(runtime + "/dde-dock-input-source.lock");
    QJsonObject result;
    if (runtime.isEmpty()) result = fail(QStringLiteral("无法确定当前用户的运行目录"));
    else if (!lock.tryLock(0)) {
        result = fail(lock.error() == QLockFile::LockFailedError
            ? QStringLiteral("另一个检测或切换操作正在进行，请稍后重试")
            : QStringLiteral("无法创建操作锁，请检查运行目录权限：%1").arg(runtime));
    } else result = args[1] == "scan" ? scan() : switchInput(args);
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    return result.value("ok").toBool() ? 0 : 1;
}
