#pragma once

#include <QFrame>

#include <functional>

class QLabel;
class QHBoxLayout;
class KuiperButton;

// banner-message (subtle emphasis): a tinted, bordered block carrying a
// title and body, plus an optional right-aligned action button. status is
// info | success | warning | danger | highlight.
class KuiperBanner : public QFrame {
    Q_OBJECT
public:
    explicit KuiperBanner(const QString& status, const QString& title,
                       const QString& message = QString(),
                       QWidget* parent = nullptr);

    void setStatus(const QString& status);
    void setTitle(const QString& title);
    void setMessage(const QString& message);

    // Show a right-aligned action button that runs `onClick`; clearAction hides
    // it. The button is created lazily on first use.
    void setAction(const QString& text, std::function<void()> onClick);
    void clearAction();

private:
    QHBoxLayout* row_;
    QLabel* title_;
    QLabel* message_;
    KuiperButton* action_ = nullptr;
    QMetaObject::Connection actionConn_;
};
