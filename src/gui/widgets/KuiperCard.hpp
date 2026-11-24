#pragma once

#include <QFrame>

class QVBoxLayout;

// A card: container surface, 1px divider, 8px radius. Content is laid
// out vertically with the design's default padding/gap; add children via the
// layout accessors.
class KuiperCard : public QFrame {
    Q_OBJECT
public:
    explicit KuiperCard(QWidget* parent = nullptr);

    void addWidget(QWidget* w);
    void addLayout(QLayout* l);

private:
    QVBoxLayout* layout_;
};
