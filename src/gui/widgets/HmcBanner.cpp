#include "HmcBanner.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "HmcButton.hpp"
#include "util/Repolish.hpp"

HmcBanner::HmcBanner(const QString& status, const QString& title,
                     const QString& message, QWidget* parent)
    : QFrame(parent) {
    setProperty("hmcBanner", true);
    setProperty("status", status);

    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(12, 12, 12, 12);
    row_->setSpacing(12);

    auto* col = new QVBoxLayout();
    col->setContentsMargins(0, 0, 0, 0);
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

    row_->addLayout(col, 1);  // text takes the width; the action sits at the end
}

void HmcBanner::setStatus(const QString& status) {
    setProperty("status", status);
    kuiper::gui::repolish(this);
}

void HmcBanner::setTitle(const QString& title) { title_->setText(title); }

void HmcBanner::setMessage(const QString& message) {
    message_->setText(message);
    message_->setVisible(!message.isEmpty());
}

void HmcBanner::setAction(const QString& text, std::function<void()> onClick) {
    if (!action_) {
        action_ = new HmcButton(this);
        action_->setKind("secondary");
        row_->addWidget(action_, 0, Qt::AlignVCenter);
    }
    action_->setText(text);
    action_->setVisible(true);
    QObject::disconnect(actionConn_);
    actionConn_ = QObject::connect(action_, &HmcButton::clicked, this,
                                   [cb = std::move(onClick)] { if (cb) cb(); });
}

void HmcBanner::clearAction() {
    if (action_) action_->setVisible(false);
}
