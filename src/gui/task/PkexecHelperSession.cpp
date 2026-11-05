#include "PkexecHelperSession.hpp"

#include <QProcess>

PkexecHelperSession::PkexecHelperSession(QObject* parent)
    : IHelperSession(parent), proc_(new QProcess(this)) {
    connect(proc_, &QProcess::readyReadStandardOutput, this, [this] {
        emit bytesReceived(proc_->readAllStandardOutput());
    });
    connect(proc_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                if (done_) return;
                done_ = true;
                Outcome o;
                if (status != QProcess::NormalExit) {
                    o = Outcome::Crashed;
                } else if (code == 126) {
                    o = Outcome::AuthCancelled;  // pkexec: auth dismissed/failed
                } else if (code == 127) {
                    o = Outcome::HelperNotFound;  // pkexec: helper not executable
                } else {
                    o = Outcome::Ok;
                }
                emit finished(code, o);
            });
    // If the program cannot be launched, finished() never fires — surface it.
    // Normally that program is pkexec (polkit missing), not the helper.
    connect(proc_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError e) {
                if (done_ || e != QProcess::FailedToStart) return;
                done_ = true;
                emit finished(-1, direct_ ? Outcome::HelperNotFound
                                          : Outcome::LaunchFailed);
            });
}

void PkexecHelperSession::start(const QString& helperPath,
                                const QStringList& verbArgs) {
    done_ = false;
    // Normally elevate via pkexec. KUIPER_HELPER_NO_PKEXEC runs the helper
    // directly (for tests in an already-root environment, e.g. CI containers).
    QString program;
    QStringList args;
    direct_ = qEnvironmentVariableIsSet("KUIPER_HELPER_NO_PKEXEC");
    if (direct_) {
        program = helperPath;
        args = verbArgs;
    } else {
        program = QStringLiteral("pkexec");
        args = QStringList{helperPath} << verbArgs;
    }
    proc_->start(program, args);
}

void PkexecHelperSession::writeCancel() {
    if (proc_->state() != QProcess::NotRunning) proc_->write("cancel\n");
}

bool PkexecHelperSession::running() const {
    return proc_->state() != QProcess::NotRunning;
}

IHelperSession* makeHelperSession(QObject* parent) {
    return new PkexecHelperSession(parent);
}
