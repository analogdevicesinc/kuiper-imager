#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

// The transport+launcher seam beneath HelperProcess. One privileged write runs in
// the kuiper-imager-helper process; *how* it is launched and talked to differs per
// OS, but the wire protocol (kuiper/ipc/HelperProtocol, NDJSON) does not. This
// interface hides the OS mechanism so HelperProcess owns only the shared pieces:
// line buffering, protocol decode, and the Outcome -> Error translation.
//
//   - Linux/macOS: pkexec + inherited stdio      (PkexecHelperSession)
//   - Windows:     ShellExecuteEx("runas") + a    (UacHelperSession)
//                  named pipe (stdio can't cross
//                  the UAC boundary)
//
// A session emits raw helper->GUI bytes as they arrive and a single finished()
// when the process ends. Everything above parses those bytes identically.
class IHelperSession : public QObject {
    Q_OBJECT
public:
    // Process-level result, distinct from a protocol `error` line. HelperProcess
    // only consults this when the helper produced no terminal result/error line.
    enum class Outcome {
        Ok,              // exited normally; trust the protocol stream for the result
        AuthCancelled,   // user declined elevation (pkexec 126 / UAC ERROR_CANCELLED)
        HelperNotFound,  // the helper binary could not be launched/found
        Crashed,         // abnormal termination
        LaunchFailed,    // elevation/launch machinery itself failed
    };
    Q_ENUM(Outcome)

    using QObject::QObject;

    // Launch the helper at `helperPath` with `verbArgs` (e.g. {"flash", "--image",
    // ...}). The session adds whatever elevation/transport plumbing it needs.
    virtual void start(const QString& helperPath, const QStringList& verbArgs) = 0;

    // Cooperative cancel: deliver a "cancel\n" line toward the helper. No-op if
    // nothing is running.
    virtual void writeCancel() = 0;

    virtual bool running() const = 0;

signals:
    // A chunk of the helper's NDJSON stream (not necessarily line-aligned).
    void bytesReceived(const QByteArray& chunk);
    // Fires exactly once when the helper process has ended (or failed to launch).
    void finished(int exitCode, IHelperSession::Outcome outcome);
};

// Defined by whichever platform session is compiled into the target.
IHelperSession* makeHelperSession(QObject* parent);
