#pragma once

#include <QColor>
#include <QWidget>

class QTimer;

// Harmonic spinner: an indeterminate rotating arc. size is "sm" (16px) or "md"
// (24px). The arc colour is a styleable property (qproperty-arcColor in the
// generated QSS) so it tracks the active theme like every other widget; it
// defaults to the light interactive-primary token as a fallback.
class HmcSpinner : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor arcColor READ arcColor WRITE setArcColor)
public:
    explicit HmcSpinner(const QString& size = "sm", QWidget* parent = nullptr);

    QColor arcColor() const { return arcColor_; }
    void setArcColor(const QColor& c) {
        arcColor_ = c;
        update();
    }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    int extent_;
    int angle_ = 0;
    QTimer* timer_;
    QColor arcColor_;
};
