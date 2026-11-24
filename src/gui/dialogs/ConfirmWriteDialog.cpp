#include "ConfirmWriteDialog.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include "widgets/KuiperBanner.hpp"
#include "widgets/KuiperButton.hpp"

ConfirmWriteDialog::ConfirmWriteDialog(const QString& target,
                                       const QString& image, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Erase and write?");
    setModal(true);
    setMinimumWidth(460);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(24, 24, 24, 24);
    col->setSpacing(20);

    col->addWidget(new KuiperBanner(
        "danger", "All data on " + target + " will be permanently erased.",
        "Writing " + image + " replaces the entire contents of the drive. This "
        "cannot be undone."));

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);

    auto* cancel = new KuiperButton("Cancel");
    cancel->setKind("secondary");
    connect(cancel, &KuiperButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);

    auto* write = new KuiperButton("Erase and write");
    connect(write, &KuiperButton::clicked, this, &QDialog::accept);
    buttons->addWidget(write);

    col->addLayout(buttons);
}
