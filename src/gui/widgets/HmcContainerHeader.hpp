#pragma once

#include <QWidget>

class QLabel;

// Harmonic container-header: a heading with an optional subtitle, used at the top
// of cards and screen sections.
class HmcContainerHeader : public QWidget {
    Q_OBJECT
public:
    explicit HmcContainerHeader(const QString& heading,
                                const QString& subtitle = QString(),
                                QWidget* parent = nullptr);

    void setHeading(const QString& text);
    void setSubtitle(const QString& text);
    // "low" bumps the heading to 20px (screen titles); default is section size.
    void setDensity(const QString& density);

private:
    QLabel* heading_;
    QLabel* subtitle_;
};
