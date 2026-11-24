#include "wrappedtext.h"

#include <QTextLayout>
#include <QTextOption>

namespace WrappedText {

qreal layout(QTextLayout &layout, qreal width)
{
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    qreal height = 0;
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(width);
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    layout.endLayout();
    return height;
}

} // namespace WrappedText
