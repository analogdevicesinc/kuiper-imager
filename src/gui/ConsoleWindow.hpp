#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

#include "kuiper/ConfigurationService.hpp"
#include "kuiper/Drive.hpp"
#include "kuiper/DriveService.hpp"
#include "kuiper/ImageService.hpp"
#include "kuiper/Layout.hpp"
#include "kuiper/MountedPartition.hpp"
#include "kuiper/Project.hpp"
#include "kuiper/Release.hpp"

class QCloseEvent;
class QComboBox;
class QLabel;
class HmcBanner;
class HmcButton;
class HmcCheckBox;
class HmcProgressBar;
class HmcSegmentedControl;
class HmcSpinner;
class HmcTag;
class BackgroundTask;
class HelperProcess;

// The main app window: pick an image (local file or downloaded stable release),
// pick a storage device, then Write — or, when the card is already a flashed
// Kuiper 2, Configure it. Write and Configure trade primary emphasis based on
// what the user has selected (see applyState()). All blocking core calls run on
// a BackgroundTask worker.
class ConsoleWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ConsoleWindow(QWidget* parent = nullptr);

protected:
    // A running job holds `this`, so closing is deferred: the job is cancelled
    // and the window closes once it has finished (see closePending_).
    void closeEvent(QCloseEvent* event) override;

private:
    // A mounted, identified Kuiper card. Holds the RAII mount alive (it unmounts
    // only what we mounted) for as long as the card stays selected.
    struct BootContext {
        kuiper::Drive drive;
        kuiper::KuiperLayout layout;
        kuiper::MountedPartition boot;
        // Present iff kuiper-release.json was found on BOOT — this is what marks
        // the card as a supported Kuiper 2 image.
        std::optional<kuiper::KuiperRelease> release;
        std::vector<kuiper::Project> projects;
    };
    struct ProbeResult {
        std::optional<BootContext> ctx;  // set iff a BOOT partition was mounted
        QString note;                    // diagnostic when not a Kuiper card
    };

    QWidget* buildImageCard();
    QWidget* buildStorageCard();
    QWidget* buildActionArea();

    // Re-enumerate drives. If preferNode names a drive still present, keep it
    // selected across the refresh (used to re-probe the just-written card).
    void refreshDrives(const QString& preferNode = QString());
    void onDriveChanged();
    void probeCurrentDrive();
    const kuiper::Drive* currentDrive() const;

    // Hand the held BOOT context (and its mount) to a worker job for release.
    // Dropping it unmounts what we mounted, which shells out to udisksctl and
    // can block, so it must not run on the GUI thread; the job resets the
    // returned holder first, ordering the unmount before anything it mounts.
    std::shared_ptr<std::optional<BootContext>> takeBootContext();

    void pickImage();       // run the current source's picker
    void chooseRelease();   // list releases → pick → fetch
    void startFetch(const QString& id, const QString& label);
    void setLocalImage(const QString& path);
    void startWrite();
    void openConfigure();

    void applyState();
    void setBusyUi(bool busy);
    void showResult(const QString& status, const QString& title,
                    const QString& message);

    // True while any operation is in flight — thread-based (task_) or the
    // elevated helper (helper_). All the start guards and applyState use this.
    bool busy() const;

    kuiper::DriveService drives_;
    kuiper::ImageService images_;
    kuiper::ConfigurationService config_;
    BackgroundTask* task_;
    HelperProcess* helper_;

    std::vector<kuiper::Drive> driveList_;
    std::optional<BootContext> bootCtx_;
    bool closePending_ = false;  // close requested while busy; finish when idle
    QString imagePath_;
    QString imageLabel_;
    bool probing_ = false;

    // Post-write re-probe retry: after a flash, re-enumerate + re-probe the
    // written card until it is recognised as Kuiper 2 or the budget runs out.
    // The helper settles the new partition table before exiting, so this almost
    // always succeeds on the first try; the retry only covers a slow udisks.
    QString writeReprobeNode_;
    int writeReprobeLeft_ = 0;

    HmcSegmentedControl* source_;
    QLabel* imageValue_;
    QLabel* imageCaption_;
    HmcTag* imageTag_;
    HmcButton* imageBtn_;

    QComboBox* driveCombo_;
    HmcButton* rescanBtn_;
    HmcTag* removableTag_;
    HmcTag* systemTag_;
    HmcTag* kuiperTag_;
    HmcSpinner* probeSpinner_;
    QLabel* probeLabel_;

    HmcCheckBox* verify_;
    HmcButton* writeBtn_;
    HmcButton* configureBtn_;

    QWidget* buttonRow_;
    QWidget* progressRow_;
    HmcProgressBar* progress_;
    HmcButton* cancelBtn_;
    HmcBanner* result_;

    std::shared_ptr<std::optional<kuiper::Result<kuiper::DriveList>>> drivesOut_;
    std::shared_ptr<ProbeResult> probeOut_;
    std::shared_ptr<std::optional<kuiper::Result<std::vector<kuiper::Release>>>>
        releasesOut_;
    std::shared_ptr<std::optional<kuiper::Result<kuiper::FetchSummary>>> fetchOut_;
    // Note: the flash result lives in helper_->result() (it runs elevated).
};
