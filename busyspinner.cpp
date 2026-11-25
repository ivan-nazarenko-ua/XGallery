#include "busyspinner.h"

#include <QPainter>

#include <cmath>

namespace {

constexpr int Dots = 8;
constexpr int StepMs = 100;
constexpr double Pi = 3.14159265358979323846;

} // namespace

BusySpinner::BusySpinner(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_timer.setInterval(StepMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_step = (m_step + 1) % Dots;
        update();
    });
    hide();
}

void BusySpinner::start()
{
    show();
    if (!m_timer.isActive())
        m_timer.start();
}

void BusySpinner::stop()
{
    m_timer.stop();
    hide();
}

QSize BusySpinner::sizeHint() const
{
    const int side = qMax(16, fontMetrics().height() + 4);
    return {side, side};
}

void BusySpinner::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const qreal side = qMin(width(), height());
    const qreal dot = side / 6.0;
    const qreal radius = side / 2.0 - dot;
    const QPointF centre(width() / 2.0, height() / 2.0);
    QColor color = palette().color(QPalette::Highlight);
    for (int i = 0; i < Dots; ++i) {
        // The running dot is full colour, the ones behind it fade out.
        const int behind = (m_step - i + Dots) % Dots;
        color.setAlphaF(1.0 - behind * 0.8 / Dots);
        painter.setBrush(color);
        const qreal angle = 2 * Pi * i / Dots - Pi / 2;
        painter.drawEllipse(centre + QPointF(radius * std::cos(angle), radius * std::sin(angle)), dot / 2 + 0.5, dot / 2 + 0.5);
    }
}
