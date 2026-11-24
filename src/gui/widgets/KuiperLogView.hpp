#pragma once

#include <QWidget>

class QPlainTextEdit;

// Append-only, read-only monospace console log for streamed command output (e.g. a
// customize run's apt output). Bounds its own memory (last kMaxBlocks lines), wraps
// rather than scrolling sideways so a long line can't widen the parent, and follows
// the tail only when the user is already at the bottom.
class KuiperLogView : public QWidget {
    Q_OBJECT
public:
    explicit KuiperLogView(QWidget* parent = nullptr);

    void appendLine(const QString& line);
    void clear();

private:
    QPlainTextEdit* edit_;
};
