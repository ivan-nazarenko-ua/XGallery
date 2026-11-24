#ifndef TRASHPROMPT_H
#define TRASHPROMPT_H

#include "scanner.h"

class QWidget;

namespace TrashPrompt {

// Asks what to do with a file that didn't go to the Recycle Bin (GUI thread).
// several = one file of a batch: NotRecyclable offers "this one" / "all such" / stop, Failed offers
// continue / stop. A single file: NotRecyclable asks "delete permanently?" (DeletePermanently or Stop),
// Failed only explains (Stop).
Scanner::TrashChoice ask(QWidget *parent, const QString &title, const QString &path,
                         Scanner::TrashProblem problem, const QString &error, bool several);

} // namespace TrashPrompt

#endif // TRASHPROMPT_H