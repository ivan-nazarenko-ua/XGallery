#ifndef BUSYDIALOG_H
#define BUSYDIALOG_H

#include <QDialog>

#include <functional>

class QLabel;
class QProgressBar;
class QPushButton;

// Modal "please wait" dialog that the user cannot close. A job that can stop offers a
// "Перервати" button (setStopHandler).
class BusyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BusyDialog(QWidget *parent = nullptr);

    void setText(const QString &text);
    // min == max == 0 gives an indeterminate ("busy") bar.
    void setRange(int minimum, int maximum);
    void setValue(int value);
    // Shows "Перервати", which calls onStop once and then waits for the job (empty: no button).
    void setStopHandler(std::function<void()> onStop);

public slots:
    void reject() override; // ignore Esc

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QLabel *m_label;
    QProgressBar *m_bar;
    QPushButton *m_stopButton;
    std::function<void()> m_onStop;
};

#endif // BUSYDIALOG_H
