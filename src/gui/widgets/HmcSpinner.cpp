#include "HmcSpinner.hpp"

#include <QPainter>
#include <QTimer>

#include "theme/Tokens.hpp"

HmcSpinner::HmcSpinner(const QString& size, QWidget* parent)
    : QWidget(parent), extent_(size == "md" ? 24 : 16) {
    setFixedSize(extent_, extent_);
    timer_ = new QTimer(this);
    timer_->setInterval(16);  // ~60fps
    connect(timer_, &QTimer::timeout, this, [this] {
        angle_ = (angle_ + 8) % 360;
        update();
    });
}

QSize HmcSpinner::sizeHint() const { return {extent_, extent_}; }

void HmcSpinner::showEvent(QShowEvent*) { timer_->start(); }
void HmcSpinner::hideEvent(QHideEvent*) { timer_->stop(); }

void HmcSpinner::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal pen = extent_ >= 24 ? 3.0 : 2.0;
    const qreal inset = pen / 2 + 1;
    const QRectF ring(inset, inset, extent_ - 2 * inset, extent_ - 2 * inset);

    // Faint full track + a bright 90° arc that sweeps.
    QColor base = kuiper::theme::light::interactivePrimary;
    QColor track = base;
    track.setAlpha(40);
    p.setPen(QPen(track, pen));
    p.drawArc(ring, 0, 360 * 16);

    p.setPen(QPen(base, pen, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(ring, -angle_ * 16, 90 * 16);
}
