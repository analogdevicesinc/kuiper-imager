#pragma once

#include <QFrame>

class QLabel;

// Harmonic banner-message (subtle emphasis): a tinted, bordered block carrying a
// title and body. status is info | success | warning | danger | highlight.
class HmcBanner : public QFrame {
    Q_OBJECT
public:
    explicit HmcBanner(const QString& status, const QString& title,
                       const QString& message = QString(),
                       QWidget* parent = nullptr);

    void setStatus(const QString& status);
    void setTitle(const QString& title);
    void setMessage(const QString& message);

private:
    QLabel* title_;
    QLabel* message_;
};
