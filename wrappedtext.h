#ifndef WRAPPEDTEXT_H
#define WRAPPEDTEXT_H

#include <QtGlobal>

class QTextLayout;

namespace WrappedText {

// Lays text out in lines of the given width: at word boundaries, a word longer than a line is
// broken anywhere (like CSS overflow-wrap: break-word). Returns the height of the text.
qreal layout(QTextLayout &layout, qreal width);

} // namespace WrappedText

#endif // WRAPPEDTEXT_H
