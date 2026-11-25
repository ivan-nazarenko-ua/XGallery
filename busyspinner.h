#ifndef BUSYSPINNER_H
#define BUSYSPINNER_H

#include <QTimer>
#include <QWidget>

// A small circle of dots with one running round: shows that the program is waiting, not hung.
// Hidden while stopped.
class BusySpinner : public QWidget
{
    Q_OBJECT

public:
    explicit BusySpinner(QWidget *parent = nullptr);

    void start();
    void stop();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTimer m_timer;
    int m_step = 0;
};

#endif // BUSYSPINNER_H
