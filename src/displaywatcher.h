#pragma once
#include <QObject>

struct udev;
struct udev_monitor;
class QSocketNotifier;

// Receive kernel/udev DRM connection changes even for outputs outside the
// desktop layout. Qt screen signals remain available if udev is unavailable.
class DisplayWatcher : public QObject {
    Q_OBJECT
public:
    explicit DisplayWatcher(QObject *parent = nullptr);
    ~DisplayWatcher() override;
signals:
    void changed();
private:
    udev *m_udev = nullptr;
    udev_monitor *m_monitor = nullptr;
    QSocketNotifier *m_notifier = nullptr;
};
