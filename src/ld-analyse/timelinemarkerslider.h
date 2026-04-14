#ifndef TIMELINEMARKERSLIDER_H
#define TIMELINEMARKERSLIDER_H

#include <QSlider>
#include <QVector>
#include <QtGlobal>

class TimelineMarkerSlider : public QSlider
{
    Q_OBJECT

public:
    explicit TimelineMarkerSlider(QWidget *parent = nullptr);
    void setMarkerFrames(qint32 inFrame, qint32 outFrame, const QVector<qint32> &noteFrames);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qint32 inFrame_ = -1;
    qint32 outFrame_ = -1;
    QVector<qint32> noteFrames_;
};

#endif // TIMELINEMARKERSLIDER_H
