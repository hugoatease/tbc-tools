#include "timelinemarkerslider.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

TimelineMarkerSlider::TimelineMarkerSlider(QWidget *parent)
    : QSlider(parent)
{
}

void TimelineMarkerSlider::setMarkerFrames(qint32 inFrame, qint32 outFrame, const QVector<qint32> &noteFrames)
{
    if (inFrame_ == inFrame && outFrame_ == outFrame && noteFrames_ == noteFrames) {
        return;
    }

    inFrame_ = inFrame;
    outFrame_ = outFrame;
    noteFrames_ = noteFrames;
    update();
}

void TimelineMarkerSlider::paintEvent(QPaintEvent *event)
{
    QSlider::paintEvent(event);

    if (orientation() != Qt::Horizontal || maximum() <= minimum()) {
        return;
    }

    QStyleOptionSlider option;
    initStyleOption(&option);
    const QRect grooveRect = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
    if (!grooveRect.isValid() || grooveRect.width() <= 1) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const int trackLength = grooveRect.width() - 1;
    const int lineTop = grooveRect.top() - 3;
    const int lineBottom = grooveRect.bottom() + 3;

    auto drawMarkerLine = [&](qint32 framePosition, const QColor &color) {
        if (framePosition < minimum() || framePosition > maximum()) {
            return;
        }
        const int sliderOffset = QStyle::sliderPositionFromValue(minimum(), maximum(), framePosition, trackLength, option.upsideDown);
        const int x = grooveRect.left() + sliderOffset;
        QPen pen(color);
        pen.setWidth(2);
        painter.setPen(pen);
        painter.drawLine(x, lineTop, x, lineBottom);
    };

    drawMarkerLine(inFrame_, QColor(24, 190, 24));
    drawMarkerLine(outFrame_, QColor(220, 45, 45));
    for (qint32 framePosition : noteFrames_) {
        drawMarkerLine(framePosition, QColor(30, 120, 255));
    }
}
