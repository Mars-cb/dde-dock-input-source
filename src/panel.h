#pragma once
#include "controller.h"
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>
#include <QWidget>

class Panel : public QWidget {
    Q_OBJECT
public:
    explicit Panel(Controller *controller, QWidget *parent = nullptr);
protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    void rebuild();
    void updateAppearance();
    Controller *m_controller;
    QLabel *m_subtitle, *m_status;
    QPushButton *m_refresh;
    QWidget *m_content;
    QVBoxLayout *m_cards;
    QSet<QString> m_manual;
};
