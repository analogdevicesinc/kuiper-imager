#pragma once

#include <QDialog>
#include <QString>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "kuiper/ImageCustomizer.hpp"

class QLabel;
class QLineEdit;
class HmcCheckBox;
class HmcBanner;
class HmcButton;
class HmcProgressBar;
class HmcLogView;
class QScrollArea;
class HelperProcess;

// Modal "Customize image" dialog: pick ADI features and install them into a local
// basic .img in place. The install is privileged, so it runs in the elevated
// helper via a HelperProcess the dialog owns. Linux-only. See docs:
// customize-pipeline.
class CustomizeDialog : public QDialog {
    Q_OBJECT
public:
    CustomizeDialog(const QString& imagePath, QWidget* parent = nullptr);

    // Esc / title-bar close: refused while a customize run is in flight (the
    // helper is a child of this dialog, and the image is mid-modification). Use
    // the Cancel button, which asks the helper to stop at a feature boundary.
    void reject() override;

private:
    std::vector<std::string> selectedIds() const;
    void updateEstimate();  // refresh the "~added / final" label on toggles
    void run();
    void setRunning(bool running);
    void onFinished();

    QString imagePath_;
    std::uint64_t imageBytes_ = 0;
    HelperProcess* helper_;

    std::vector<std::pair<std::string, HmcCheckBox*>> checks_;  // id -> checkbox
    QScrollArea* scroll_;  // the feature list; hidden by the log during a run
    QLabel* estimate_;
    HmcCheckBox* grow_;    // auto-grow the rootfs to fit (default on)
    QLineEdit* growSize_;  // optional explicit size (e.g. "4G"); overrides auto
    QString scriptPath_;      // host script run as root in the chroot (optional)
    HmcButton* scriptBtn_;    // picks the script
    QLabel* scriptLabel_;     // shows the chosen script's name, or a hint
    HmcButton* scriptClear_;  // clears the chosen script
    HmcBanner* result_;
    HmcProgressBar* progress_;  // indeterminate activity + coarse phase heading
    HmcLogView* log_;           // streamed helper/apt output (the real readout)
    HmcButton* runBtn_;
    HmcButton* closeBtn_;  // "Close", or "Cancel" while running
};
