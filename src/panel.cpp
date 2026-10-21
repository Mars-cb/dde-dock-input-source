#include "panel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QEvent>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QShowEvent>
#include <QSpinBox>

static QLabel *label(const QString &text, QWidget *parent = nullptr)
{
    auto *widget = new QLabel(text, parent);
    widget->setTextFormat(Qt::PlainText);
    widget->setWordWrap(true);
    return widget;
}
Panel::Panel(Controller *controller, QWidget *parent) : QWidget(parent), m_controller(controller)
{
    setObjectName("inputSourcePanel");
    // The dock popup already supplies the blurred, translucent background.
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setWindowTitle(QStringLiteral("显示器输入源"));
    setFixedWidth(360);
    resize(360, 530);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 14);
    layout->setSpacing(12);
    auto *header = new QHBoxLayout;
    auto *title = label(QStringLiteral("显示器输入源"));
    QFont titleFont = title->font(); titleFont.setPointSize(14); titleFont.setBold(true); title->setFont(titleFont);
    header->addWidget(title, 1);
    m_refresh = new QPushButton(QStringLiteral("刷新"));
    m_refresh->setAccessibleName(QStringLiteral("重新检测所有显示器"));
    header->addWidget(m_refresh);
    layout->addLayout(header);
    m_subtitle = label(QStringLiteral("检测所有显示器，分别切换输入信号"));
    layout->addWidget(m_subtitle);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("monitorScrollArea");
    scroll->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget;
    m_content->setObjectName("monitorContent");
    m_cards = new QVBoxLayout(m_content);
    m_cards->setContentsMargins(0, 0, 0, 0);
    m_cards->setSpacing(10);
    scroll->setWidget(m_content);
    // setWidget() enables autofill; turn it off after attaching the content.
    m_content->setAutoFillBackground(false);
    layout->addWidget(scroll, 1);
    m_status = label(QString());
    m_status->setMinimumHeight(36);
    m_status->setAccessibleName(QStringLiteral("操作状态"));
    layout->addWidget(m_status);
    auto *note = label(QStringLiteral("切换到其他设备后，此电脑可能失去画面和显示器控制。"));
    QFont small = note->font(); small.setPointSize(qMax(8, small.pointSize()-1)); note->setFont(small);
    layout->addWidget(note);
    connect(m_refresh, &QPushButton::clicked, controller, &Controller::refresh);
    connect(controller, &Controller::changed, this, &Panel::rebuild);
    connect(controller, &Controller::busyChanged, this, [this](bool busy) {
        m_refresh->setEnabled(!busy); m_content->setEnabled(!busy);
        m_refresh->setText(busy ? QStringLiteral("处理中…") : QStringLiteral("刷新"));
    });
    connect(controller, &Controller::message, this, [this](const QString &text, bool error) {
        m_status->setText(text);
        m_status->setStyleSheet(error ? "color: #c44236;" : QString());
    });
    updateAppearance();
    rebuild();
}
void Panel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_controller->refresh();
}
void Panel::rebuild()
{
    while (auto *item = m_cards->takeAt(0)) {
        if (item->widget()) { item->widget()->hide(); item->widget()->deleteLater(); }
        delete item;
    }
    m_subtitle->setText(QStringLiteral("%1 个显示器%2").arg(m_controller->monitors().size())
                       .arg(m_controller->demo() ? QStringLiteral(" · 演示模式") : QString()));
    if (m_controller->monitors().isEmpty()) m_cards->addWidget(label(QStringLiteral("尚未发现显示器，点击刷新重新检测。")));
    for (const Monitor &m : m_controller->monitors()) {
        auto *card = new QFrame;
        card->setObjectName("monitorCard");
        card->setAutoFillBackground(false);
        card->setFrameShape(QFrame::NoFrame);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(12, 12, 12, 12);
        auto *name = label(m.name);
        QFont font = name->font(); font.setBold(true); name->setFont(font);
        layout->addWidget(name);
        QString detail = m.connector;
        if (m.bus >= 0) detail += QStringLiteral(" · I²C %1").arg(m.bus);
        if (!m.serial.isEmpty()) detail += QStringLiteral(" · %1").arg(m.serial);
        layout->addWidget(label(detail));
        layout->addWidget(label(QStringLiteral("当前输入：%1").arg(inputName(m.current))));
        if (!m.error.isEmpty()) layout->addWidget(label(m.error));
        if (m.controllable) {
            const bool manual = m_manual.contains(m.id);
            auto *row = new QHBoxLayout;
            auto *combo = new QComboBox;
            combo->setAccessibleName(m.name + QStringLiteral("的目标输入源"));
            combo->addItem(QStringLiteral("选择输入源…"), -1);
            QVector<int> values = m.sources;
            if (manual) for (int n : {1, 3, 4, 15, 16, 17, 18}) if (!values.contains(n)) values.append(n);
            for (int n : values) {
                QString text = inputName(n);
                if (n == m.current) text += QStringLiteral("（当前）");
                else if (!m.sources.contains(n)) text += QStringLiteral("（未报告）");
                combo->addItem(text, n);
            }
            if (manual) combo->addItem(QStringLiteral("自定义 VCP 值…"), 0);
            auto *button = new QPushButton(QStringLiteral("切换"));
            button->setEnabled(false);
            row->addWidget(combo, 1); row->addWidget(button);
            layout->addLayout(row);
            auto *custom = new QSpinBox;
            custom->setRange(1, 255); custom->setDisplayIntegerBase(16); custom->setPrefix("0x");
            custom->setAccessibleName(QStringLiteral("自定义输入源的十六进制 VCP 值"));
            custom->setValue(0x11); custom->hide(); layout->addWidget(custom);
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), card,
                    [combo, custom, button, m](int) {
                const int n = combo->currentData().toInt();
                custom->setVisible(n == 0);
                button->setEnabled(n >= 0 && n != m.current);
            });
            connect(button, &QPushButton::clicked, this, [this, combo, custom, m, manual] {
                const int n = combo->currentData().toInt();
                m_controller->switchSource(m.id, n == 0 ? custom->value() : n, manual);
            });
            auto *advanced = new QCheckBox(QStringLiteral("手动模式（显示器未报告的输入源）"));
            advanced->setChecked(manual);
            advanced->setToolTip(QStringLiteral("仅在确认显示器支持该 VCP 值时使用；USB-C 的编号由厂商定义。"));
            layout->addWidget(advanced);
            if (manual) layout->addWidget(label(QStringLiteral("手动值需查阅显示器资料；未连接的输入源可能导致黑屏。")));
            connect(advanced, &QCheckBox::toggled, this, [this, id = m.id](bool enabled) {
                if (enabled) m_manual.insert(id); else m_manual.remove(id);
                rebuild();
            });
        }
        m_cards->addWidget(card);
    }
    m_cards->addStretch();
    m_content->setEnabled(!m_controller->busy());
}

void Panel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
            || event->type() == QEvent::StyleChange)
        updateAppearance();
}
void Panel::updateAppearance()
{
    // Derive the overlay tint from the text palette so it follows dock themes.
    const QColor tint = palette().color(QPalette::WindowText);
    const QString rgb = QStringLiteral("%1, %2, %3").arg(tint.red()).arg(tint.green()).arg(tint.blue());
    const QString sheet = QStringLiteral(
        "QWidget#inputSourcePanel, QWidget#monitorContent, QScrollArea#monitorScrollArea {"
        " background: transparent; border: none; }"
        "QLabel, QCheckBox { background: transparent; }"
        "QFrame#monitorCard { background: rgba(%1, 10); border: 1px solid rgba(%1, 22); border-radius: 8px; }"
        "QPushButton { background: rgba(%1, 16); border: 1px solid rgba(%1, 24);"
        " border-radius: 6px; padding: 5px 12px; }"
        "QPushButton:hover { background: rgba(%1, 29); }"
        "QPushButton:pressed { background: rgba(%1, 42); }"
        "QPushButton:disabled { background: rgba(%1, 8); border-color: rgba(%1, 12); }"
        "QComboBox, QSpinBox { background: rgba(%1, 12); border: 1px solid rgba(%1, 25);"
        " border-radius: 6px; padding: 5px 8px; }"
        "QComboBox::drop-down { background: transparent; border: none; width: 20px; }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
        "QScrollBar::handle:vertical { background: rgba(%1, 55); border-radius: 4px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(%1, 85); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; background: transparent; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
    ).arg(rgb);
    if (styleSheet() != sheet) setStyleSheet(sheet);
}
