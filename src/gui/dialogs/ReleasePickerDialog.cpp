#include "ReleasePickerDialog.hpp"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "util/Format.hpp"
#include "widgets/HmcButton.hpp"
#include "widgets/HmcContainerHeader.hpp"

ReleasePickerDialog::ReleasePickerDialog(
    const std::vector<kuiper::Release>& releases, QWidget* parent)
    : QDialog(parent), releases_(releases) {
    setWindowTitle("Choose a release");
    setModal(true);
    resize(560, 420);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(24, 24, 24, 24);
    col->setSpacing(16);

    col->addWidget(new HmcContainerHeader(
        "Stable releases", "ADI Kuiper Linux — published images"));

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({"Version", "Variant", "Arch", "Size"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);

    table_->setRowCount(static_cast<int>(releases_.size()));
    for (int row = 0; row < static_cast<int>(releases_.size()); ++row) {
        const auto& r = releases_[row];
        auto* version = new QTableWidgetItem(QString::fromStdString(r.version));
        // Stash the fetch id on the first column so accept() can read it back.
        version->setData(Qt::UserRole, QString::fromStdString(r.id));
        table_->setItem(row, 0, version);
        table_->setItem(row, 1,
                        new QTableWidgetItem(QString::fromStdString(r.variant)));
        table_->setItem(row, 2,
                        new QTableWidgetItem(QString::fromStdString(r.arch)));
        table_->setItem(row, 3,
                        new QTableWidgetItem(kuiper::gui::formatBytes(r.sizeBytes)));
    }
    if (!releases_.empty()) table_->selectRow(0);
    connect(table_, &QTableWidget::doubleClicked, this, &QDialog::accept);
    col->addWidget(table_, 1);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* cancel = new HmcButton("Cancel");
    cancel->setKind("secondary");
    connect(cancel, &HmcButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    auto* choose = new HmcButton("Download");
    connect(choose, &HmcButton::clicked, this, &QDialog::accept);
    buttons->addWidget(choose);
    col->addLayout(buttons);
}

void ReleasePickerDialog::accept() {
    const int row = table_->currentRow();
    if (row >= 0 && row < static_cast<int>(releases_.size())) {
        const auto& r = releases_[row];
        selectedId_ = QString::fromStdString(r.id);
        selectedLabel_ = QString::fromStdString(r.version + " · " + r.variant +
                                                " · " + r.arch);
    }
    QDialog::accept();
}
