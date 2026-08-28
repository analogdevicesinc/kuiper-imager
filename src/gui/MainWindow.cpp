#include "MainWindow.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include "kuiper/Version.hpp"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Kuiper Imager 2");
    resize(720, 400);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* refreshBtn = new QPushButton("Refresh drives", central);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshDrives);
    layout->addWidget(refreshBtn);

    table_ = new QTableWidget(central);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {"Drive", "Description", "Size (GB)", "Removable", "System"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_);

    setCentralWidget(central);

    status_ = new QLabel(this);
    statusBar()->addWidget(status_);
    status_->setText(QString("libkuiper %1  ·  backend: %2")
                         .arg(kuiper::version())
                         .arg(driveService_.backendName()));

    refreshDrives();
}

void MainWindow::refreshDrives() {
    const auto result = driveService_.listDrives();
    if (!result) {
        status_->setText(QString("Error [%1]: %2")
                             .arg(kuiper::toString(result.error().code))
                             .arg(QString::fromStdString(result.error().message)));
        table_->setRowCount(0);
        return;
    }

    const auto& drives = *result;
    table_->setRowCount(static_cast<int>(drives.size()));
    for (int row = 0; row < static_cast<int>(drives.size()); ++row) {
        const auto& d = drives[row];
        table_->setItem(row, 0,
                        new QTableWidgetItem(QString::fromStdString(d.node)));
        table_->setItem(row, 1,
                        new QTableWidgetItem(QString::fromStdString(d.description)));
        table_->setItem(row, 2,
                        new QTableWidgetItem(QString::number(
                            static_cast<double>(d.sizeBytes) / 1e9, 'f', 1)));
        table_->setItem(row, 3,
                        new QTableWidgetItem(d.isRemovable ? "yes" : "no"));
        table_->setItem(row, 4, new QTableWidgetItem(d.isSystem ? "yes" : "no"));
    }
    status_->setText(QString("%1 drive(s)  ·  backend: %2")
                         .arg(drives.size())
                         .arg(driveService_.backendName()));
}
