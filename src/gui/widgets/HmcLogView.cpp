#include "HmcLogView.hpp"

#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextOption>
#include <QVBoxLayout>

namespace {
// A desktop-environment install can emit thousands of apt lines; cap the buffer so
// a long run can't grow unbounded. The tail is what matters, so the oldest lines
// are dropped first (QPlainTextEdit::maximumBlockCount).
constexpr int kMaxBlocks = 5000;
}  // namespace

HmcLogView::HmcLogView(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);

    edit_ = new QPlainTextEdit(this);
    edit_->setReadOnly(true);
    edit_->setProperty("hmcLog", true);
    edit_->setFrameShape(QFrame::NoFrame);  // the border comes from QSS
    edit_->setMaximumBlockCount(kMaxBlocks);
    // Wrap instead of scrolling sideways: a long apt line must never force the
    // dialog wider (the old single-line phase label did exactly that).
    edit_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    edit_->setWordWrapMode(QTextOption::WrapAnywhere);
    edit_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    col->addWidget(edit_);
}

void HmcLogView::appendLine(const QString& line) {
    // Follow the tail only if the user is already there; otherwise leave their
    // scroll position alone so they can read back through the output mid-run.
    auto* sb = edit_->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;
    edit_->appendPlainText(line);
    if (atBottom) sb->setValue(sb->maximum());
}

void HmcLogView::clear() { edit_->clear(); }
