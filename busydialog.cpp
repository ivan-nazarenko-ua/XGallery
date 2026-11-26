#include "busydialog.h"

#include "busyspinner.h"

#include <QCloseEvent>
#include <QLabel>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

BusyDialog::BusyDialog(QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint)
    , m_label(new QLabel(this))
    , m_bar(new QProgressBar(this))
    , m_stopButton(new QPushButton(this))
    , m_spinner(new BusySpinner(this))
{
    setWindowTitle(tr("Зачекай…"));
    setModal(true);
    setMinimumWidth(420);

    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    m_bar->setRange(0, 0);

    m_stopButton->setAutoDefault(false);
    m_stopButton->hide();
    connect(m_stopButton, &QPushButton::clicked, this, [this] {
        if (m_confirmStop && !m_confirmStop())
            return;
        m_stopButton->setEnabled(false);
        m_stopButton->setText(tr("Зупиняю…"));
        if (m_onStop)
            m_onStop();
    });
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_stopButton);
    buttonRow->addStretch(1);

    auto *textRow = new QHBoxLayout;
    textRow->addWidget(m_spinner, 0, Qt::AlignTop);
    textRow->addWidget(m_label, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(textRow);
    layout->addWidget(m_bar);
    layout->addLayout(buttonRow);
}

void BusyDialog::setStopHandler(std::function<void()> onStop)
{
    m_onStop = std::move(onStop);
    m_confirmStop = {};
    m_stopButton->setText(tr("Перервати"));
    m_stopButton->setEnabled(true);
    m_stopButton->setVisible(bool(m_onStop));
}

void BusyDialog::setStopConfirmation(std::function<bool()> confirm)
{
    m_confirmStop = std::move(confirm);
}

void BusyDialog::setSpinning(bool spinning)
{
    if (spinning)
        m_spinner->start();
    else
        m_spinner->stop();
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
