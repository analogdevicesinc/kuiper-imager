#pragma once

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;

// Harmonic segmented-control: a row of mutually-exclusive options inside a
// bordered frame. Used for small either/or choices (e.g. Local file / Download).
class HmcSegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit HmcSegmentedControl(QWidget* parent = nullptr);

    // Add a segment; `value` is returned by value()/emitted by valueChanged.
    void addSegment(const QString& text, const QString& value);
    QString value() const;
    void setValue(const QString& value);

signals:
    void valueChanged(const QString& value);

private:
    void relayoutPositions();

    QButtonGroup* group_;
    QHBoxLayout* row_;
};
