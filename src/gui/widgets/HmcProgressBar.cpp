#include "HmcProgressBar.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

HmcProgressBar::HmcProgressBar(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);  // Harmonic progress-bar gap

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    phase_ = new QLabel(this);
    detail_ = new QLabel(this);
    detail_->setProperty("hmcCaption", true);
    detail_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(phase_, 1);
    row->addWidget(detail_, 0);
    col->addLayout(row);

    bar_ = new QProgressBar(this);
    bar_->setProperty("hmcProgress", true);
    bar_->setTextVisible(false);
    bar_->setRange(0, 1000);
    col->addWidget(bar_);
}

void HmcProgressBar::setPhase(const QString& text) { phase_->setText(text); }
void HmcProgressBar::setDetail(const QString& text) { detail_->setText(text); }

void HmcProgressBar::setFraction(double f) {
    f = std::clamp(f, 0.0, 1.0);
    bar_->setValue(static_cast<int>(std::lround(f * 1000)));
}

void HmcProgressBar::setStatus(const QString& status) {
    bar_->setProperty("status", status);
    bar_->style()->unpolish(bar_);
    bar_->style()->polish(bar_);
}

void HmcProgressBar::setIndeterminate(bool on) {
    bar_->setRange(0, on ? 0 : 1000);
}
