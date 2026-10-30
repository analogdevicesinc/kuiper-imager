#include "HmcBanner.hpp"

#include <QFont>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

HmcBanner::HmcBanner(const QString& status, const QString& title,
                     const QString& message, QWidget* parent)
    : QFrame(parent) {
    setProperty("hmcBanner", true);
    setProperty("status", status);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 12, 12, 12);
    col->setSpacing(4);

    title_ = new QLabel(title, this);
    QFont f = title_->font();
    f.setWeight(QFont::DemiBold);
    title_->setFont(f);
    title_->setWordWrap(true);
    col->addWidget(title_);

    message_ = new QLabel(message, this);
    message_->setWordWrap(true);
    message_->setVisible(!message.isEmpty());
    col->addWidget(message_);
}

void HmcBanner::setStatus(const QString& status) {
    setProperty("status", status);
    style()->unpolish(this);
    style()->polish(this);
}

void HmcBanner::setTitle(const QString& title) { title_->setText(title); }

void HmcBanner::setMessage(const QString& message) {
    message_->setText(message);
    message_->setVisible(!message.isEmpty());
}
