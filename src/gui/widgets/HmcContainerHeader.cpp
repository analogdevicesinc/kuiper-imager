#include "HmcContainerHeader.hpp"

#include <QLabel>
#include <QVBoxLayout>

#include "util/Repolish.hpp"

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

void HmcContainerHeader::setDensity(const QString& density) {
    heading_->setProperty("density", density);
    kuiper::gui::repolish(heading_);
}
