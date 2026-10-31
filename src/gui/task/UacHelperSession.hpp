#pragma once

#include <memory>

#include "task/IHelperSession.hpp"

class QLocalSocket;
class QWinEventNotifier;

// Windows elevation session. UAC elevation (ShellExecuteEx "runas") routes the
// helper through the AppInfo service, so it is NOT a child of the GUI and does not
// inherit stdio — the Linux stdio transport cannot cross that boundary. Instead the
// unprivileged GUI creates a named pipe and passes its name to the helper, which
// connects back as a client. The pipe carries the exact same NDJSON bytes; only the
// transport differs.
//
// The pipe is created directly (not via QLocalServer) so we own its security:
//   - DACL: the current user + BUILTIN\Administrators. The latter is what lets a
//     standard user elevate with *another* account's credentials (over-the-shoulder
//     UAC): the helper then runs as that admin, not as us. QLocalServer can only
//     grant the current user, which would lock that helper out.
//   - one instance, FILE_FLAG_FIRST_PIPE_INSTANCE (a pre-squatted name fails
//     creation), PIPE_REJECT_REMOTE_CLIENTS, a fresh UUID name.
//   - an identity check that fails closed: the connected client must be the exact
//     process we launched (GetNamedPipeClientProcessId); anything else is dropped.
// Once accepted, the connected handle is handed to a QLocalSocket for async I/O.
//
// The write parameters (verb, image, drive) travel as command-line arguments at
// elevation time — UAC-consented — never over the pipe, which only ever carries
// progress/result/error out and "cancel" in. A hijacked pipe can therefore at most
// cancel or spoof progress text, never change what is written where.
//
// KUIPER_HELPER_NO_ELEVATE launches the helper without "runas" (still over the
// pipe); valid only when the caller is already elevated — the Windows analog of the
// "already-root" KUIPER_HELPER_NO_PKEXEC bypass.
class UacHelperSession : public IHelperSession {
    Q_OBJECT
public:
    explicit UacHelperSession(QObject* parent = nullptr);
    ~UacHelperSession() override;

    void start(const QString& helperPath, const QStringList& verbArgs) override;
    void writeCancel() override;
    bool running() const override;

private:
    struct Pipe;  // the listening pipe + its overlapped connect (Win32 types)

    void reset();             // tear down any previous run's state
    bool armConnect();        // start an overlapped ConnectNamedPipe
    void completeConnect();   // reap a finished connect, if any
    void acceptClient();      // identity check, then hand off to QLocalSocket
    void onProcessExit();
    void finishOnce(int exitCode, Outcome outcome);
    void closeProcessHandle();

    std::unique_ptr<Pipe> pipe_;
    QWinEventNotifier* connectNotifier_ = nullptr;  // pipe connect completed
    QLocalSocket* sock_ = nullptr;
    QWinEventNotifier* exitNotifier_ = nullptr;     // helper process exited
    void* proc_ = nullptr;         // HANDLE to the helper process (opaque here)
    unsigned long helperPid_ = 0;  // DWORD; for the client-PID identity check
    bool running_ = false;
    bool done_ = false;            // guards a single finished()
    bool pendingCancel_ = false;   // cancel arrived before the helper connected
};
