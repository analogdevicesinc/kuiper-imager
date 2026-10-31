#pragma once

#include <QDialog>
#include <QString>

// A destructive-action confirmation: a danger banner naming exactly what will be
// erased, with Cancel / Write. exec() returns QDialog::Accepted on confirm.
class ConfirmWriteDialog : public QDialog {
    Q_OBJECT
public:
    // `target` is the human summary of the drive (e.g. "KIOXIA EXCERIA
    // (/dev/sdb) · 1.0 TB"); `image` is the image filename.
    ConfirmWriteDialog(const QString& target, const QString& image,
                       QWidget* parent = nullptr);
};
