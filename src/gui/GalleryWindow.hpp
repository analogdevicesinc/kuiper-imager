#pragma once

#include <QMainWindow>

// A component gallery for designing by hand: renders one of each Hmc* widget so
// the skins can be eyeballed (and screenshotted) in isolation. Shown with
// `kuiper-imager --gallery`.
class GalleryWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit GalleryWindow(QWidget* parent = nullptr);
};
