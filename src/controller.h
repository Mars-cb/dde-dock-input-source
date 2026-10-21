#pragma once
#include "model.h"
#include <QObject>
#include <QProcess>
#include <QTimer>

class Controller : public QObject {
    Q_OBJECT
public:
    explicit Controller(bool demo = false, QObject *parent = nullptr);
    ~Controller() override;
    const QVector<Monitor> &monitors() const { return m_monitors; }
    bool busy() const { return m_busy; }
    bool demo() const { return m_demo; }
public slots:
    void refresh();
    void switchSource(const QString &id, int value, bool manual = false);
signals:
    void changed();
    void busyChanged(bool busy);
    void message(const QString &text, bool error);
private:
    void start(const QStringList &args, bool switching);
    void finish(int exitCode, QProcess::ExitStatus exitStatus);
    void complete(bool error, const QString &message);
    void addDesktopScreens();
    QVector<Monitor> m_monitors;
    QProcess m_process;
    QTimer m_timeout, m_hotplug;
    bool m_busy = false, m_demo = false, m_switching = false, m_timedOut = false;
    bool m_refreshPending = false;
};
