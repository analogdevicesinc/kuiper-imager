#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;

// Harmonic progress-bar: a labelled 8px bar with a trailing detail (percentage or
// byte readout). status drives the fill colour: info | success | danger.
class HmcProgressBar : public QWidget {
    Q_OBJECT
public:
    explicit HmcProgressBar(QWidget* parent = nullptr);

    void setPhase(const QString& text);   // left label, e.g. "Writing…"
    void setDetail(const QString& text);  // right label, e.g. "5.9 / 8.0 GB"
    void setFraction(double f);           // [0,1]
    void setStatus(const QString& status);
    void setIndeterminate(bool on);

private:
    QLabel* phase_;
    QLabel* detail_;
    QProgressBar* bar_;
};
