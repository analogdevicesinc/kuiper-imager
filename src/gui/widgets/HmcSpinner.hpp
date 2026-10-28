#pragma once

#include <QWidget>

class QTimer;

// Harmonic spinner: an indeterminate rotating arc. size is "sm" (16px) or "md"
// (24px). Painted with the Harmonic interactive-primary token (Tokens.hpp) — a
// small example of using the generated colour constants in QPainter code.
class HmcSpinner : public QWidget {
    Q_OBJECT
public:
    explicit HmcSpinner(const QString& size = "sm", QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    int extent_;
    int angle_ = 0;
    QTimer* timer_;
};
