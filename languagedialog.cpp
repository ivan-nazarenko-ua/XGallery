#include "languagedialog.h"

#include "i18n.h"

#include <QIcon>
#include <QPushButton>
#include <QVBoxLayout>

LanguageDialog::LanguageDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("XGallery")); // the only text besides the language names
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    QFont font = this->font();
    font.setPointSizeF(font.pointSizeF() * 1.3);

    for (const I18n::Language &language : I18n::languages()) {
        auto *button = new QPushButton(QIcon(language.flag), language.nativeName, this);
        button->setIconSize(QSize(48, 32));
        button->setFont(font);
        button->setMinimumSize(280, 60);
        button->setStyleSheet("QPushButton { text-align: left; padding-left: 16px; }"); // flag on the left
        button->setAutoDefault(false);
        connect(button, &QPushButton::clicked, this, [this, code = language.code] {
            m_chosen = code;
            accept();
        });
        layout->addWidget(button);
    }
}

QString LanguageDialog::ask()
{
    LanguageDialog dialog;
    return dialog.exec() == QDialog::Accepted ? dialog.m_chosen : QString();
}
