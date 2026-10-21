#pragma once
#include "panel.h"
#include <pluginsiteminterface.h>
#include <QPointer>

class InputSourcePlugin : public QObject, public PluginsItemInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.deepin.dock.PluginsItemInterface" FILE "input-source.json")
    Q_INTERFACES(PluginsItemInterface)
public:
    ~InputSourcePlugin() override;
    const QString pluginName() const override { return QStringLiteral("input-source"); }
    const QString pluginDisplayName() const override { return QStringLiteral("显示器输入源"); }
    void init(PluginProxyInterface *proxy) override;
    QWidget *itemWidget(const QString &) override;
    QWidget *itemTipsWidget(const QString &) override;
    QWidget *itemPopupApplet(const QString &) override;
    bool pluginIsAllowDisable() override { return true; }
    bool pluginIsDisable() override;
    void pluginStateSwitched() override;
    int itemSortKey(const QString &) override;
    void setSortKey(const QString &, int order) override;
private:
    Controller *m_controller = nullptr;
    QPointer<QWidget> m_icon;
    QPointer<QLabel> m_tip;
    QPointer<Panel> m_panel;
};
