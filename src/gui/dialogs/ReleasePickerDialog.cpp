#include "ReleasePickerDialog.hpp"

#include <QAbstractItemView>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include "util/Format.hpp"
#include "widgets/KuiperButton.hpp"
#include "widgets/KuiperContainerHeader.hpp"
#include "widgets/KuiperTag.hpp"

namespace {
constexpr int kColVersion = 0;
constexpr int kColVariant = 1;
constexpr int kColArch = 2;
constexpr int kColSize = 3;
constexpr int kColStatus = 4;
}  // namespace

ReleasePickerDialog::ReleasePickerDialog(
    const std::vector<kuiper::Release>& releases, CachedPathFn cachedPathOf,
    QWidget* parent)
    : QDialog(parent),
      releases_(releases),
      cachedPathOf_(std::move(cachedPathOf)) {
    setWindowTitle("Choose a release");
    setModal(true);
    resize(620, 420);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(24, 24, 24, 24);
    col->setSpacing(16);

    col->addWidget(new KuiperContainerHeader(
        "Stable releases", "ADI Kuiper Linux — published images"));

    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {"Version", "Variant", "Arch", "Size", "Status"});
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
        table_->setItem(row, kColVersion, version);
        table_->setItem(row, kColVariant,
                        new QTableWidgetItem(QString::fromStdString(r.variant)));
        table_->setItem(row, kColArch,
                        new QTableWidgetItem(QString::fromStdString(r.arch)));
        table_->setItem(row, kColSize, new QTableWidgetItem());
        refreshStatusCell(row);
    }
    if (!releases_.empty()) table_->selectRow(0);
    connect(table_, &QTableWidget::doubleClicked, this, &QDialog::accept);
    col->addWidget(table_, 1);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* cancel = new KuiperButton("Cancel");
    cancel->setKind("secondary");
    connect(cancel, &KuiperButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    auto* choose = new KuiperButton("Download");
    connect(choose, &KuiperButton::clicked, this, &QDialog::accept);
    buttons->addWidget(choose);
    col->addLayout(buttons);
}

void ReleasePickerDialog::refreshStatusCell(int row) {
    const auto& r = releases_[row];
    const QString cached = cachedPathOf_ ? cachedPathOf_(r) : QString();

    // Size: the real on-disk size once downloaded, else the catalog's size (0 for
    // stable, so show a dash rather than "0 B").
    QString sizeText = "—";
    if (!cached.isEmpty())
        sizeText = kuiper::gui::formatBytes(QFileInfo(cached).size());
    else if (r.sizeBytes > 0)
        sizeText = kuiper::gui::formatBytes(r.sizeBytes);
    table_->item(row, kColSize)->setText(sizeText);

    table_->removeCellWidget(row, kColStatus);
    if (cached.isEmpty()) return;

    // "Downloaded" badge + an in-place delete that reclaims the cached file.
    auto* cell = new QWidget(table_);
    auto* lay = new QHBoxLayout(cell);
    lay->setContentsMargins(6, 2, 6, 2);
    lay->setSpacing(6);
    lay->addWidget(new KuiperTag("Downloaded", "success"));
    lay->addStretch(1);
    auto* del = new KuiperButton("🗑");
    del->setKind("ghost");
    del->setToolTip("Delete this download");
    connect(del, &KuiperButton::clicked, this, [this, row] { deleteCached(row); });
    lay->addWidget(del);
    table_->setCellWidget(row, kColStatus, cell);
}

void ReleasePickerDialog::deleteCached(int row) {
    const auto& r = releases_[row];
    const QString cached = cachedPathOf_ ? cachedPathOf_(r) : QString();
    if (cached.isEmpty()) return;

    const QString label = QString::fromStdString(r.version + " · " + r.variant +
                                                 " · " + r.arch);
    if (QMessageBox::question(
            this, "Delete download",
            "Delete the downloaded image for " + label +
                "?\n\nIt can be downloaded again later.") != QMessageBox::Yes)
        return;

    if (!QFile::remove(cached)) {
        QMessageBox::warning(this, "Delete failed",
                             "Could not delete the cached file:\n" + cached);
    }
    refreshStatusCell(row);  // reflect the new (not-cached) state
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
