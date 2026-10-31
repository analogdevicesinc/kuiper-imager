#pragma once

#include <QDialog>
#include <QString>
#include <vector>

#include "kuiper/Release.hpp"

class QTableWidget;

// Picks one release from a pre-fetched list (the window does the async
// listReleases()). exec() returns Accepted; selectedId() is the Release::id to
// hand to ImageService::fetch(), or empty if nothing was chosen.
class ReleasePickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ReleasePickerDialog(const std::vector<kuiper::Release>& releases,
                                 QWidget* parent = nullptr);

    QString selectedId() const { return selectedId_; }
    QString selectedLabel() const { return selectedLabel_; }

private:
    void accept() override;

    const std::vector<kuiper::Release>& releases_;
    QTableWidget* table_;
    QString selectedId_;
    QString selectedLabel_;
};
