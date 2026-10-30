#pragma once

#include <QFrame>

class QVBoxLayout;

// A Harmonic card: container surface, 1px divider, 8px radius. Content is laid
// out vertically with the design's default padding/gap; add children via the
// layout accessors.
class HmcCard : public QFrame {
    Q_OBJECT
public:
    explicit HmcCard(QWidget* parent = nullptr);

    void addWidget(QWidget* w);
    void addLayout(QLayout* l);
    QVBoxLayout* content() const { return layout_; }

private:
    QVBoxLayout* layout_;
};
