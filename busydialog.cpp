#include "busydialog.h"

#include <QCloseEvent>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

BusyDialog::BusyDialog(QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint)
    , m_label(new QLabel(this))
    , m_bar(new QProgressBar(this))
{
    setWindowTitle(tr("Зачекай…"));
    setModal(true);
    setMinimumWidth(420);

    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    m_bar->setRange(0, 0);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_label);
    layout->addWidget(m_bar);
}

void BusyDialog::setText(const QString &text)
{
    m_label->setText(text);
}

void BusyDialog::setRange(int minimum, int maximum)
{
    m_bar->setRange(minimum, maximum);
}

void BusyDialog::setValue(int value)
{
    m_bar->setValue(value);
}

void BusyDialog::reject()
{
}

void BusyDialog::closeEvent(QCloseEvent *event)
{
    event->ignore();
}
