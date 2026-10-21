#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct Monitor {
    QString id, name, connector, serial, edid, error;
    int bus = -1;
    int current = -1;
    bool controllable = false;
    QVector<int> sources;
    QJsonObject toJson() const;
    static Monitor fromJson(const QJsonObject &object);
};
QString inputName(int value);
QString connectorName(QString name);
QString edidText(const QByteArray &edid, unsigned char tag);
QVector<Monitor> drmMonitors(const QString &root = QStringLiteral("/sys/class/drm"));
// Never infer a target from list order, model name, or a duplicated EDID.
int matchingMonitor(const QVector<Monitor> &monitors, int bus, const QString &edid);
bool validTarget(int bus, const QString &edid, int value);
bool allowedSource(const Monitor &monitor, int value, bool manual);
