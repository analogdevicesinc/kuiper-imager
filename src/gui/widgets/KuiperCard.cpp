#include "KuiperCard.hpp"

#include <QVBoxLayout>

KuiperCard::KuiperCard(QWidget* parent) : QFrame(parent) {
    setProperty("kuiperCard", true);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(20, 20, 20, 20);  // medium-density card
    layout_->setSpacing(12);
}

void KuiperCard::addWidget(QWidget* w) { layout_->addWidget(w); }
void KuiperCard::addLayout(QLayout* l) { layout_->addLayout(l); }
