#include "displaywatcher.h"
#include <QSocketNotifier>
#include <QDebug>
#include <libudev.h>

DisplayWatcher::DisplayWatcher(QObject *parent) : QObject(parent)
{
    m_udev = udev_new();
    if (m_udev) m_monitor = udev_monitor_new_from_netlink(m_udev, "udev");
    if (!m_monitor
            || udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "drm", nullptr) < 0
            || udev_monitor_enable_receiving(m_monitor) < 0
            || udev_monitor_get_fd(m_monitor) < 0) {
        qWarning() << "DRM hotplug monitoring unavailable; using Qt screen notifications";
        return;
    }
    m_notifier = new QSocketNotifier(udev_monitor_get_fd(m_monitor), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] {
        bool received = false;
        while (auto *device = udev_monitor_receive_device(m_monitor)) {
            received = true;
            udev_device_unref(device);
        }
        if (received) emit changed();
    });
}
DisplayWatcher::~DisplayWatcher()
{
    // Stop watching the fd before unref closes it.
    delete m_notifier;
    if (m_monitor) udev_monitor_unref(m_monitor);
    if (m_udev) udev_unref(m_udev);
}
