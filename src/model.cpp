#include "model.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

QJsonObject Monitor::toJson() const
{
    QJsonArray values;
    for (int value : sources) values.append(value);
    return {{"id", id}, {"name", name}, {"connector", connector}, {"serial", serial},
            {"edid", edid}, {"bus", bus}, {"current", current}, {"sources", values},
            {"controllable", controllable}, {"error", error}};
}
Monitor Monitor::fromJson(const QJsonObject &o)
{
    Monitor m;
    m.id = o.value("id").toString(); m.name = o.value("name").toString();
    m.connector = o.value("connector").toString(); m.serial = o.value("serial").toString();
    m.edid = o.value("edid").toString(); m.bus = o.value("bus").toInt(-1);
    m.current = o.value("current").toInt(-1); m.error = o.value("error").toString();
    m.controllable = o.value("controllable").toBool();
    for (const auto &v : o.value("sources").toArray()) {
        const int n = v.toInt(-1);
        if (n > 0 && n <= 255 && !m.sources.contains(n)) m.sources.append(n);
    }
    return m;
}
QString inputName(int value)
{
    switch (value) {
    case 0x01: return QStringLiteral("VGA 1");
    case 0x02: return QStringLiteral("VGA 2");
    case 0x03: return QStringLiteral("DVI 1");
    case 0x04: return QStringLiteral("DVI 2");
    case 0x05: return QStringLiteral("Composite 1");
    case 0x06: return QStringLiteral("Composite 2");
    case 0x07: return QStringLiteral("S-Video 1");
    case 0x08: return QStringLiteral("S-Video 2");
    case 0x0c: return QStringLiteral("Component 1");
    case 0x0d: return QStringLiteral("Component 2");
    case 0x0e: return QStringLiteral("Component 3");
    case 0x0f: return QStringLiteral("DisplayPort 1");
    case 0x10: return QStringLiteral("DisplayPort 2");
    case 0x11: return QStringLiteral("HDMI 1");
    case 0x12: return QStringLiteral("HDMI 2");
    default:
        return value < 0 ? QStringLiteral("未知")
                         : QStringLiteral("输入源 0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
    }
}
QString connectorName(QString name)
{
    name.remove(QRegularExpression(QStringLiteral("^card[0-9]+-")));
    // DRM uses HDMI-A-1, while X11 commonly uses HDMI-1.
    name.replace(QStringLiteral("HDMI-A-"), QStringLiteral("HDMI-"));
    name.replace(QStringLiteral("DisplayPort-"), QStringLiteral("DP-"));
    return name;
}
QString edidText(const QByteArray &edid, unsigned char tag)
{
    if (edid.size() < 128) return {};
    for (int at = 54; at <= 108; at += 18) {
        if (edid[at] == 0 && edid[at+1] == 0 && edid[at+2] == 0
                && static_cast<unsigned char>(edid[at+3]) == tag)
            return QString::fromLatin1(edid.mid(at+5, 13)).trimmed();
    }
    return {};
}
static QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QVector<Monitor> drmMonitors(const QString &root)
{
    QVector<Monitor> result;
    const QDir dir(root);
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!QRegularExpression(QStringLiteral("^card[0-9]+-.+")).match(entry).hasMatch()) continue;
        const QString path = dir.filePath(entry);
        if (readFile(path + "/status").trimmed() != "connected") continue;
        const QByteArray edid = readFile(path + "/edid").left(128);
        Monitor m;
        m.id = entry; m.connector = connectorName(entry);
        m.name = edidText(edid, 0xfc); m.serial = edidText(edid, 0xff);
        if (m.name.isEmpty()) m.name = m.connector;
        if (edid.size() == 128) m.edid = QString::fromLatin1(edid.toHex());
        const QString ddc = QFileInfo(path + "/ddc").canonicalFilePath();
        const auto match = QRegularExpression(QStringLiteral("/i2c-([0-9]+)$")).match(ddc);
        if (match.hasMatch()) m.bus = match.captured(1).toInt();
        // AMD Display Core can expose ddc -> ordinary hardware I2C while
        // DisplayPort DDC/CI actually uses an AUX adapter below the connector.
        // Prefer only a unique AUX child belonging to this exact connector.
        int auxBus = -1;
        bool ambiguousAux = false;
        const QDir connectorDir(path);
        for (const auto &child : connectorDir.entryList({"i2c-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto childMatch = QRegularExpression(QStringLiteral("^i2c-([0-9]+)$")).match(child);
            const auto busName = readFile(connectorDir.filePath(child + "/name")).trimmed();
            if (!childMatch.hasMatch() || !busName.toLower().contains("aux")) continue;
            if (auxBus >= 0) ambiguousAux = true;
            auxBus = childMatch.captured(1).toInt();
        }
        if (auxBus >= 0 && !ambiguousAux) m.bus = auxBus;
        if (m.connector.startsWith("eDP") || m.connector.startsWith("LVDS") || m.connector.startsWith("DSI"))
            m.error = QStringLiteral("内置屏幕不支持外部输入源切换");
        else
            m.error = QStringLiteral("未检测到可访问的 DDC/CI 接口，请检查显示器设置及 I²C 权限");
        result.append(m);
    }
    return result;
}
int matchingMonitor(const QVector<Monitor> &monitors, int bus, const QString &edid)
{
    if (edid.isEmpty()) return -1;
    int exact = -1;
    for (int i = 0; i < monitors.size(); ++i) {
        if (bus >= 0 && monitors[i].bus == bus && monitors[i].edid == edid) {
            if (exact >= 0) return -1;
            exact = i;
        }
    }
    if (exact >= 0) return exact;
    int unique = -1;
    for (int i = 0; i < monitors.size(); ++i) {
        // A known conflicting bus must never be replaced through EDID alone.
        if (monitors[i].edid == edid && monitors[i].bus < 0) {
            if (unique >= 0) return -1;
            unique = i;
        }
    }
    return unique;
}
bool validTarget(int bus, const QString &edid, int value)
{
    return bus >= 0 && bus <= 65535 && value > 0 && value <= 255
            && QRegularExpression(QStringLiteral("^[0-9a-fA-F]{256}$")).match(edid).hasMatch();
}
bool allowedSource(const Monitor &monitor, int value, bool manual)
{
    return monitor.controllable && validTarget(monitor.bus, monitor.edid, value)
            && (monitor.sources.contains(value) || manual);
}
