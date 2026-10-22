#include "panel.h"
#include <QCheckBox>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QEvent>
#include <QHBoxLayout>
#include <QListView>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QScrollArea>
#include <QSpinBox>
#include <DBlurEffectWidget>

DWIDGET_USE_NAMESPACE

// Use the same DTK behind-window blur as the dock's popup surface.
class InputSourceComboBox : public QComboBox {
public:
    InputSourceComboBox() {
        setStyleSheet(QStringLiteral("QComboBox { combobox-popup: 0; }"));
        setAutoFillBackground(false);
        QPalette colors = palette();
        colors.setColor(QPalette::Base, Qt::transparent);
        setPalette(colors);
        auto *list = new QListView;
        setView(list);
        setItemDelegate(new QStyledItemDelegate(list));
        view()->window()->setAttribute(Qt::WA_TranslucentBackground);
    }
    void showPopup() override {
        auto *list = view();
        auto *popup = list->window();
        popup->setAttribute(Qt::WA_TranslucentBackground);
        popup->setAutoFillBackground(false);
        if (m_popup != popup) {
            m_popup = popup;
            popup->installEventFilter(this);
            m_blur = new DBlurEffectWidget(popup);
            m_blur->setObjectName(QStringLiteral("inputSourcePopupBlur"));
            m_blur->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_blur->setBlendMode(DBlurEffectWidget::BehindWindowBlend);
            m_blur->setBlurRectXRadius(7);
            m_blur->setBlurRectYRadius(7);
            m_blur->setMaskColor(DBlurEffectWidget::AutoColor);
        }
        popup->setObjectName(QStringLiteral("inputSourceComboPopup"));
        m_blur->setPalette(QApplication::palette());
        m_blur->setGeometry(popup->rect());
        m_blur->lower();
        m_blur->show();
        list->setAutoFillBackground(false);
        list->viewport()->setAutoFillBackground(false);
        list->setFrameShape(QFrame::NoFrame);
        const QPalette colors = QApplication::palette();
        const QColor highlight = colors.color(QPalette::Highlight);
        const QString selected = QStringLiteral("rgba(%1, %2, %3, 160)")
            .arg(highlight.red()).arg(highlight.green()).arg(highlight.blue());
        popup->setStyleSheet(QStringLiteral(
            "QAbstractItemView { background: transparent; border: none; outline: 0; }"
            "QAbstractItemView::item { background: transparent; min-height: 26px; padding: 3px 8px; border-radius: 4px; }"
            "QAbstractItemView::item:selected, QAbstractItemView::item:hover { background: %1; color: %2; }"
        ).arg(selected, colors.color(QPalette::HighlightedText).name()));
        QComboBox::showPopup();
        m_blur->setGeometry(popup->rect());
        m_blur->lower();
        list->setAutoFillBackground(false);
        list->viewport()->setAutoFillBackground(false);
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == m_popup) {
            if (event->type() == QEvent::Resize && m_blur)
                m_blur->setGeometry(m_popup->rect());
            // The DTK background child paints the surface and requests the WM
            // blur region. Suppress the native container's opaque menu paint.
            if (event->type() == QEvent::Paint) return true;
        }
        return QComboBox::eventFilter(object, event);
    }
private:
    QPointer<QWidget> m_popup;
    QPointer<DBlurEffectWidget> m_blur;
};

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
        if (error && !m_controller->busy()) rebuild();
    });
    updateAppearance();
    rebuild();
    m_controller->initialize();
    m_refresh->setEnabled(!m_controller->busy());
    m_refresh->setText(m_controller->busy() ? QStringLiteral("处理中…") : QStringLiteral("刷新"));
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
            auto *combo = new InputSourceComboBox;
            combo->setAccessibleName(m.name + QStringLiteral("的输入源，选择后立即切换"));
            if (m.current < 0) combo->addItem(QStringLiteral("当前输入源未知"), -1);
            QVector<int> values = m.sources;
            if (m.current > 0 && !values.contains(m.current)) values.prepend(m.current);
            if (manual) for (int n : {1, 3, 4, 15, 16, 17, 18}) if (!values.contains(n)) values.append(n);
            for (int n : values) {
                QString text = inputName(n);
                if (n == m.current) text += QStringLiteral("（当前）");
                else if (!m.sources.contains(n)) text += QStringLiteral("（未报告）");
                combo->addItem(text, n);
            }
            if (manual) combo->addItem(QStringLiteral("自定义 VCP 值…"), 0);
            combo->setCurrentIndex(qMax(0, combo->findData(m.current)));
            layout->addWidget(combo);
            auto *custom = new QSpinBox;
            custom->setRange(1, 255); custom->setDisplayIntegerBase(16); custom->setPrefix("0x");
            custom->setAccessibleName(QStringLiteral("自定义输入源的十六进制 VCP 值"));
            custom->setToolTip(QStringLiteral("输入十六进制 VCP 值，按 Enter 切换"));
            custom->setValue(m.current > 0 && m.current <= 255 ? m.current : 0x11);
            custom->hide(); layout->addWidget(custom);
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), card,
                    [combo, custom](int) { custom->setVisible(combo->currentData().toInt() == 0); });
            // activated() only comes from user interaction; initial selection,
            // theme updates and monitor refreshes must never write to hardware.
            connect(combo, QOverload<int>::of(&QComboBox::activated), this,
                    [this, combo, custom, m, manual](int) {
                const int n = combo->currentData().toInt();
                if (n == 0) { custom->setFocus(); custom->selectAll(); return; }
                if (n > 0 && n != m.current) m_controller->switchSource(m.id, n, manual);
            });
            connect(custom, &QSpinBox::editingFinished, this, [this, combo, custom, m, manual] {
                // Enter commits a custom value; focus loss while choosing a
                // different item must not trigger an extra switch.
                if (custom->hasFocus() && combo->currentData().toInt() == 0 && custom->value() != m.current)
                    m_controller->switchSource(m.id, custom->value(), manual);
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
