#include "HmcCard.hpp"

#include <QVBoxLayout>

HmcCard::HmcCard(QWidget* parent) : QFrame(parent) {
    setProperty("hmcCard", true);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(20, 20, 20, 20);  // Harmonic medium-density card
    layout_->setSpacing(12);
}

void HmcCard::addWidget(QWidget* w) { layout_->addWidget(w); }
void HmcCard::addLayout(QLayout* l) { layout_->addLayout(l); }
