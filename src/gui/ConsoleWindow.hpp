#pragma once

#include <QMainWindow>

// The Kuiper Imager console: the single-screen flasher shell (choose image →
// choose drive → write). This is a design draft — the three cards are populated
// with sample state to show the "ready to write" look; it is not yet wired to the
// core services.
class ConsoleWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ConsoleWindow(QWidget* parent = nullptr);
};
