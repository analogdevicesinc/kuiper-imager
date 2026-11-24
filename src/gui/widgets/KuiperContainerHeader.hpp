#pragma once

#include <QWidget>

class QLabel;

// container-header: a heading with an optional subtitle, used at the top
// of cards and screen sections.
class KuiperContainerHeader : public QWidget {
    Q_OBJECT
public:
    explicit KuiperContainerHeader(const QString& heading,
                                const QString& subtitle = QString(),
                                QWidget* parent = nullptr);

    // "low" bumps the heading to 20px (screen titles); default is section size.
    void setDensity(const QString& density);

private:
    QLabel* heading_;
    QLabel* subtitle_;
};
