#include "ConsoleWindow.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "kuiper/Version.hpp"
#include "widgets/HmcButton.hpp"
#include "widgets/HmcCard.hpp"
#include "widgets/HmcCheckBox.hpp"
#include "widgets/HmcContainerHeader.hpp"
#include "widgets/HmcTag.hpp"

namespace {

// Each pick-card shares the same anatomy: header, a value + caption, a flexible
// gap, then an action pinned to the bottom. Returning the card lets the caller
// keep building; `action` is added last.
HmcCard* makeCard(const QString& title, QWidget* parent) {
    auto* card = new HmcCard(parent);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    card->setMinimumHeight(240);
    card->addWidget(new HmcContainerHeader(title));
    return card;
}

QLabel* valueLabel(const QString& text) {
    auto* l = new QLabel(text);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() + 1);
    f.setWeight(QFont::DemiBold);
    l->setFont(f);
    l->setWordWrap(true);
    return l;
}

QLabel* caption(const QString& text) {
    auto* l = new QLabel(text);
    l->setProperty("hmcCaption", true);
    return l;
}

QLabel* arrow() {
    auto* a = new QLabel(QStringLiteral("→"));  // →
    a->setProperty("hmcCaption", true);
    a->setAlignment(Qt::AlignCenter);
    a->setFixedWidth(24);
    return a;
}

}  // namespace

ConsoleWindow::ConsoleWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Kuiper Imager");
    resize(900, 560);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- app header bar ---
    auto* header = new QFrame(central);
    header->setProperty("hmcHeaderBar", true);
    auto* headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(24, 16, 24, 16);
    auto* wordmark = new HmcContainerHeader("Kuiper Imager", "ADI Kuiper Linux imager");
    wordmark->setDensity("low");
    headerRow->addWidget(wordmark);
    headerRow->addStretch(1);
    root->addWidget(header);

    // --- body ---
    auto* body = new QWidget(central);
    auto* bodyCol = new QVBoxLayout(body);
    bodyCol->setContentsMargins(24, 24, 24, 24);
    bodyCol->setSpacing(24);

    auto* cards = new QHBoxLayout();
    cards->setSpacing(16);

    // Image card (sample "selected" state)
    auto* imageCard = makeCard("Choose image", body);
    imageCard->addWidget(valueLabel("kuiper_full_arm64.zip"));
    imageCard->addWidget(caption("8.0 GB · .zip"));
    imageCard->content()->addStretch(1);
    {
        auto* b = new HmcButton("Change…");
        b->setKind("secondary");
        imageCard->addWidget(b);
    }

    // Drive card (sample "selected" state)
    auto* driveCard = makeCard("Choose drive", body);
    driveCard->addWidget(valueLabel("KIOXIA EXCERIA"));
    driveCard->addWidget(caption("/dev/sdb · 1.0 TB"));
    {
        auto* tagRow = new QHBoxLayout();
        tagRow->setContentsMargins(0, 0, 0, 0);
        tagRow->addWidget(new HmcTag("Removable", "success"));
        tagRow->addStretch(1);
        driveCard->addLayout(tagRow);
    }
    driveCard->content()->addStretch(1);
    {
        auto* b = new HmcButton("Change…");
        b->setKind("secondary");
        driveCard->addWidget(b);
    }

    // Write card
    auto* writeCard = makeCard("Write", body);
    writeCard->addWidget(caption("Ready to flash."));
    writeCard->content()->addStretch(1);
    {
        auto* b = new HmcButton("Write");
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        writeCard->addWidget(b);
        writeCard->addWidget(caption("Verify is on"));
    }

    cards->addWidget(imageCard, 1);
    cards->addWidget(arrow());
    cards->addWidget(driveCard, 1);
    cards->addWidget(arrow());
    cards->addWidget(writeCard, 1);
    bodyCol->addLayout(cards);

    // options row
    auto* options = new QHBoxLayout();
    auto* verify = new HmcCheckBox("Verify after writing");
    verify->setChecked(true);
    options->addWidget(verify);
    options->addStretch(1);
    {
        auto* adv = new HmcButton("Advanced ▾");
        adv->setKind("ghost");
        options->addWidget(adv);
    }
    bodyCol->addLayout(options);
    bodyCol->addStretch(1);

    root->addWidget(body, 1);
    setCentralWidget(central);

    statusBar()->showMessage(
        QString("libkuiper %1").arg(kuiper::version()));
}
