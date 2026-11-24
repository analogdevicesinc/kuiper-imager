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
class KuiperBanner;
class KuiperButton;
class KuiperCheckBox;
class KuiperProgressBar;
class KuiperSegmentedControl;
class KuiperSpinner;
class KuiperTag;
class BackgroundTask;
class HelperProcess;

// The main app window: pick an image (local file or downloaded stable release),
// pick a storage device, then Write. Each card owns the action that edits its
// object — Customize edits the image, Configure edits a flashed Kuiper 2 card —
// while Write is the single commit action below. All blocking core calls run on
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

    // How the currently selected image was obtained — drives the provenance tag
    // and gates Customize (raw local .img only).
    enum class ImageOrigin { None, Local, Downloaded };

    void pickImage();       // choose a local image file
    void chooseRelease();   // list releases → pick → reuse-cached-or-fetch
    void startFetch(const QString& id, const QString& label);
    void setLocalImage(const QString& path, ImageOrigin origin);
    void clearImage();      // drop the selection and reset the Image card display

    // The per-user cache directory downloads land in, and the path a given
    // release id would occupy there if present (empty if it isn't cached).
    QString imageCacheDir() const;
    QString cachedImagePath(const QString& id);
    void startWrite();
    void showWriteComplete();              // show the deferred post-write banner
    void startEject(const QString& node);  // unmount + power off for safe removal
    void openConfigure();
    void openCustomize();  // Linux only: add ADI features to the selected .img

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
    ImageOrigin imageOrigin_ = ImageOrigin::None;
    bool probing_ = false;

    // Post-write re-probe retry: after a flash, re-enumerate + re-probe the
    // written card until it is recognised as Kuiper 2 or the budget runs out.
    // The helper settles the new partition table before exiting, so this almost
    // always succeeds on the first try; the retry only covers a slow udisks.
    QString writeReprobeNode_;
    int writeReprobeLeft_ = 0;

    // Completion banner deferred until the re-probe settles. See docs: gui-console.
    QString pendingWriteTitle_;
    QString pendingWriteMsg_;
    QString pendingEjectNode_;  // non-empty => offer "Safe eject" for this node

    QLabel* imageValue_;
    QLabel* imageCaption_;
    KuiperTag* imageTag_;                   // provenance pill: Local | Downloaded
    KuiperButton* chooseFileBtn_;
    KuiperButton* downloadBtn_;
    KuiperButton* customizeBtn_ = nullptr;  // Linux only; nullptr elsewhere

    QComboBox* driveCombo_;
    KuiperButton* rescanBtn_;
    KuiperTag* removableTag_;
    KuiperTag* systemTag_;
    KuiperTag* kuiperTag_;
    KuiperSpinner* probeSpinner_;
    QLabel* probeLabel_;

    KuiperCheckBox* verify_;
    KuiperButton* writeBtn_;
    KuiperButton* configureBtn_;

    QWidget* buttonRow_;
    QWidget* progressRow_;
    KuiperProgressBar* progress_;
    KuiperButton* cancelBtn_;
    KuiperBanner* result_;

    std::shared_ptr<std::optional<kuiper::Result<kuiper::DriveList>>> drivesOut_;
    std::shared_ptr<ProbeResult> probeOut_;
    std::shared_ptr<std::optional<kuiper::Result<std::vector<kuiper::Release>>>>
        releasesOut_;
    std::shared_ptr<std::optional<kuiper::Result<kuiper::FetchSummary>>> fetchOut_;
    std::shared_ptr<std::optional<kuiper::Result<void>>> ejectOut_;
    // Note: the flash result lives in helper_->result() (it runs elevated).
};
