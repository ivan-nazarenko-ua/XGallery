#ifndef LANGUAGEDIALOG_H
#define LANGUAGEDIALOG_H

#include <QDialog>

// First-start language choice: three flag buttons, the program name as the only text.
class LanguageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LanguageDialog(QWidget *parent = nullptr);

    // Shows the dialog; returns the chosen code, or an empty string if it was closed.
    static QString ask();

private:
    QString m_chosen;
};

#endif // LANGUAGEDIALOG_H
