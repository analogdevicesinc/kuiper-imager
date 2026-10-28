#include "HmcContainerHeader.hpp"

#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

HmcContainerHeader::HmcContainerHeader(const QString& heading,
                                       const QString& subtitle, QWidget* parent)
    : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);

    heading_ = new QLabel(heading, this);
    heading_->setProperty("hmcHeading", true);
    col->addWidget(heading_);

    subtitle_ = new QLabel(subtitle, this);
    subtitle_->setProperty("hmcSubtitle", true);
    subtitle_->setWordWrap(true);
    subtitle_->setVisible(!subtitle.isEmpty());
    col->addWidget(subtitle_);
}

void HmcContainerHeader::setHeading(const QString& text) { heading_->setText(text); }

void HmcContainerHeader::setSubtitle(const QString& text) {
    subtitle_->setText(text);
    subtitle_->setVisible(!text.isEmpty());
}

void HmcContainerHeader::setDensity(const QString& density) {
    heading_->setProperty("density", density);
    heading_->style()->unpolish(heading_);
    heading_->style()->polish(heading_);
}
