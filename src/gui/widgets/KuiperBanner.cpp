#include "KuiperBanner.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "KuiperButton.hpp"
#include "util/Repolish.hpp"

KuiperBanner::KuiperBanner(const QString& status, const QString& title,
                     const QString& message, QWidget* parent)
    : QFrame(parent) {
    setProperty("kuiperBanner", true);
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

void KuiperBanner::setStatus(const QString& status) {
    setProperty("status", status);
    kuiper::gui::repolish(this);
}

void KuiperBanner::setTitle(const QString& title) { title_->setText(title); }

void KuiperBanner::setMessage(const QString& message) {
    message_->setText(message);
    message_->setVisible(!message.isEmpty());
}

void KuiperBanner::setAction(const QString& text, std::function<void()> onClick) {
    if (!action_) {
        action_ = new KuiperButton(this);
        action_->setKind("secondary");
        row_->addWidget(action_, 0, Qt::AlignVCenter);
    }
    action_->setText(text);
    action_->setVisible(true);
    QObject::disconnect(actionConn_);
    actionConn_ = QObject::connect(action_, &KuiperButton::clicked, this,
                                   [cb = std::move(onClick)] { if (cb) cb(); });
}

void KuiperBanner::clearAction() {
    if (action_) action_->setVisible(false);
}
