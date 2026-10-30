#include "GalleryWindow.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "widgets/HmcBanner.hpp"
#include "widgets/HmcButton.hpp"
#include "widgets/HmcCard.hpp"
#include "widgets/HmcCheckBox.hpp"
#include "widgets/HmcContainerHeader.hpp"
#include "widgets/HmcProgressBar.hpp"
#include "widgets/HmcSegmentedControl.hpp"
#include "widgets/HmcSpinner.hpp"
#include "widgets/HmcTag.hpp"

namespace {

// A small section label above each group of samples.
QLabel* sectionLabel(const QString& text) {
    auto* l = new QLabel(text);
    l->setProperty("hmcHeading", true);
    return l;
}

QWidget* row(std::initializer_list<QWidget*> widgets) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);
    for (auto* x : widgets) h->addWidget(x);
    h->addStretch(1);
    return w;
}

}  // namespace

GalleryWindow::GalleryWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Kuiper Imager — Component Gallery");
    resize(720, 900);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(24, 24, 24, 24);
    col->setSpacing(24);

    // Buttons
    col->addWidget(sectionLabel("Buttons"));
    col->addWidget(row({
        new HmcButton("Primary"),
        [] { auto* b = new HmcButton("Secondary"); b->setKind("secondary"); return b; }(),
        [] { auto* b = new HmcButton("Tertiary"); b->setKind("tertiary"); return b; }(),
        [] { auto* b = new HmcButton("Ghost"); b->setKind("ghost"); return b; }(),
        [] { auto* b = new HmcButton("Disabled"); b->setEnabled(false); return b; }(),
    }));

    // Tags
    col->addWidget(sectionLabel("Tags"));
    col->addWidget(row({
        new HmcTag("Removable", "success"),
        new HmcTag("System", "danger"),
        new HmcTag("Info", "info"),
        new HmcTag("Warning", "warning"),
        new HmcTag("Expired", "highlight"),
    }));

    // Checkboxes + spinners
    col->addWidget(sectionLabel("Checkbox & spinner"));
    auto* cbRow = row({
        new HmcCheckBox("Verify after writing"),
        [] { auto* c = new HmcCheckBox("Checked"); c->setChecked(true); return c; }(),
        new HmcSpinner("sm"),
        new HmcSpinner("md"),
    });
    col->addWidget(cbRow);

    // Segmented control
    col->addWidget(sectionLabel("Segmented control"));
    auto* seg = new HmcSegmentedControl;
    seg->addSegment("Local file", "local");
    seg->addSegment("Download", "download");
    col->addWidget(seg);

    // Progress bars
    col->addWidget(sectionLabel("Progress"));
    auto* p1 = new HmcProgressBar;
    p1->setPhase("Writing…");
    p1->setDetail("5.9 / 8.0 GB");
    p1->setFraction(0.74);
    col->addWidget(p1);
    auto* p2 = new HmcProgressBar;
    p2->setPhase("Verified");
    p2->setDetail("100%");
    p2->setFraction(1.0);
    p2->setStatus("success");
    col->addWidget(p2);

    // Banners
    col->addWidget(sectionLabel("Banners"));
    col->addWidget(new HmcBanner("info", "Heads up",
                                 "Unstable images come from CI and may be unsigned."));
    col->addWidget(new HmcBanner("danger", "Erase and write?",
                                 "All data on KIOXIA 1TB (/dev/sdb) will be permanently erased."));

    // Card with header
    col->addWidget(sectionLabel("Card"));
    auto* card = new HmcCard;
    card->addWidget(new HmcContainerHeader("Choose image", "raw, .zip, .xz, .zst, .gz"));
    card->addWidget(new HmcButton("Browse…"));
    col->addWidget(card);

    col->addStretch(1);
    scroll->setWidget(page);
    setCentralWidget(scroll);
}
