#pragma once

#include <QMainWindow>

#include "kuiper/DriveService.hpp"

class QTableWidget;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void refreshDrives();

private:
    kuiper::DriveService driveService_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
};
