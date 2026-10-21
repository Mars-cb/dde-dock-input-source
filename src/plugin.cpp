#include "plugin.h"
#include <QPainter>
#include <QPaintEvent>

class MonitorIcon : public QWidget {
public:
    MonitorIcon() { setFixedSize(16, 16); setAccessibleName(QStringLiteral("显示器输入源")); }
    QSize sizeHint() const override { return QSize(16, 16); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(palette().color(QPalette::WindowText), 1.25));
        p.drawRoundedRect(QRectF(1.25, 2.25, 13.5, 9), 1.0, 1.0);
        p.drawLine(QPointF(8, 11.25), QPointF(8, 14));
        p.drawLine(QPointF(5, 14), QPointF(11, 14));
        p.drawLine(QPointF(5, 6.75), QPointF(11, 6.75));
        p.drawLine(QPointF(9, 4.75), QPointF(11, 6.75));
        p.drawLine(QPointF(9, 8.75), QPointF(11, 6.75));
    }
};
InputSourcePlugin::~InputSourcePlugin()
{
    delete m_panel.data(); delete m_tip.data(); delete m_icon.data();
}
void InputSourcePlugin::init(PluginProxyInterface *proxy)
{
    m_proxyInter = proxy;
    if (!m_controller) m_controller = new Controller(false, this);
    if (!pluginIsDisable()) m_proxyInter->itemAdded(this, "input-source");
}
QWidget *InputSourcePlugin::itemWidget(const QString &)
{
    if (!m_icon) m_icon = new MonitorIcon;
    return m_icon;
}
QWidget *InputSourcePlugin::itemTipsWidget(const QString &)
{
    if (!m_tip) {
        m_tip = new QLabel(QStringLiteral("显示器输入源"));
        m_tip->setContentsMargins(8, 4, 8, 4);
    }
    return m_tip;
}
QWidget *InputSourcePlugin::itemPopupApplet(const QString &)
{
    if (!m_controller) return nullptr;
    if (!m_panel) m_panel = new Panel(m_controller);
    return m_panel;
}
bool InputSourcePlugin::pluginIsDisable()
{
    return m_proxyInter && m_proxyInter->getValue(this, "disabled", false).toBool();
}
void InputSourcePlugin::pluginStateSwitched()
{
    if (!m_proxyInter) return;
    const bool disable = !pluginIsDisable();
    m_proxyInter->saveValue(this, "disabled", disable);
    if (disable) m_proxyInter->itemRemoved(this, "input-source");
    else m_proxyInter->itemAdded(this, "input-source");
}
int InputSourcePlugin::itemSortKey(const QString &)
{
    return m_proxyInter ? m_proxyInter->getValue(this, "sort-key", 8).toInt() : 8;
}
void InputSourcePlugin::setSortKey(const QString &, int order)
{
    if (m_proxyInter) m_proxyInter->saveValue(this, "sort-key", order);
}
