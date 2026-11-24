#include "trashprompt.h"

#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>

namespace TrashPrompt {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("TrashPrompt", text);
}

} // namespace

Scanner::TrashChoice ask(QWidget *parent, const QString &title, const QString &path,
                         Scanner::TrashProblem problem, const QString &error, bool several)
{
    const QString file = QDir::toNativeSeparators(path);
    QMessageBox box(QMessageBox::Warning, title, QString(), QMessageBox::NoButton, parent);

    if (problem == Scanner::TrashProblem::NotRecyclable) {
        box.setText(tr("Цей файл не можна перемістити в Кошик (завеликий для Кошика, або на диску немає Кошика):\n%1\n\n"
                       "Видалити його остаточно, без можливості відновлення?").arg(file));
        QPushButton *once = box.addButton(tr("Видалити остаточно"), QMessageBox::DestructiveRole);
        QPushButton *all = several ? box.addButton(tr("Видалити остаточно всі такі"), QMessageBox::DestructiveRole)
                                   : nullptr;
        QPushButton *stop = box.addButton(several ? tr("Перервати") : tr("Скасувати"), QMessageBox::RejectRole);
        box.setDefaultButton(stop);
        box.exec();
        if (box.clickedButton() == once)
            return Scanner::TrashChoice::DeletePermanently;
        if (all && box.clickedButton() == all)
            return Scanner::TrashChoice::DeleteAllPermanently;
        return Scanner::TrashChoice::Stop;
    }

    if (!several) {
        box.setText(tr("Не вдалося перемістити файл у Кошик, нічого не видалено:\n%1\n\n%2").arg(file, error));
        box.addButton(QMessageBox::Ok);
        box.exec();
        return Scanner::TrashChoice::Stop;
    }
    box.setText(tr("Не вдалося перемістити файл у Кошик, він залишиться на місці:\n%1\n\n%2\n\n"
                   "Продовжити видалення інших файлів?").arg(file, error));
    QPushButton *continueButton = box.addButton(tr("Продовжити"), QMessageBox::AcceptRole);
    QPushButton *stop = box.addButton(tr("Перервати"), QMessageBox::RejectRole);
    box.setDefaultButton(stop);
    box.exec();
    return box.clickedButton() == continueButton ? Scanner::TrashChoice::Continue : Scanner::TrashChoice::Stop;
}

} // namespace TrashPrompt