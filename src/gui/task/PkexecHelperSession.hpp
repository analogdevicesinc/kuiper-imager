#pragma once

#include "task/IHelperSession.hpp"

class QProcess;

// Unix elevation session: launches the helper under `pkexec` (polkit) with its
// stdio inherited, so the helper's NDJSON stream arrives on stdout and "cancel"
// goes back on stdin — the original, dependency-light mechanism. Set
// KUIPER_HELPER_NO_PKEXEC to run the helper directly (tests in an already-root
// environment, e.g. CI containers).
class PkexecHelperSession : public IHelperSession {
    Q_OBJECT
public:
    explicit PkexecHelperSession(QObject* parent = nullptr);

    void start(const QString& helperPath, const QStringList& verbArgs) override;
    void writeCancel() override;
    bool running() const override;

private:
    QProcess* proc_;
    bool done_ = false;  // guards a single finished() across error/finished signals
    bool direct_ = false;  // KUIPER_HELPER_NO_PKEXEC: launching the helper itself
};
