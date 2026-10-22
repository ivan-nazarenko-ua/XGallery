#ifndef BUSYDIALOG_H
#define BUSYDIALOG_H

#include <QDialog>

class QLabel;
class QProgressBar;

// Modal "please wait" dialog that the user cannot close.
class BusyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BusyDialog(QWidget *parent = nullptr);

    void setText(const QString &text);
    // min == max == 0 gives an indeterminate ("busy") bar.
    void setRange(int minimum, int maximum);
    void setValue(int value);

public slots:
    void reject() override; // ignore Esc

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QLabel *m_label;
    QProgressBar *m_bar;
};

#endif // BUSYDIALOG_H
