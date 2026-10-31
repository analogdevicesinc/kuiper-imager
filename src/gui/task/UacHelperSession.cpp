#include "UacHelperSession.hpp"

#include <QLocalSocket>
#include <QUuid>
#include <QWinEventNotifier>

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
//
#include <sddl.h>
#include <shellapi.h>

namespace {

// Quote one argument per the CommandLineToArgvW rules, so paths with spaces (image
// paths, drive descriptions) survive the single command-line string both
// ShellExecuteEx and CreateProcess take. Mirrors the canonical MSDN algorithm.
std::wstring quoteArg(const std::wstring& arg) {
    if (!arg.empty() &&
        arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;  // no quoting needed
    }
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        unsigned backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');  // escape trailing backslashes
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');  // escape backslashes + the quote
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
}

std::wstring toW(const QString& s) { return s.toStdWString(); }

// The current process's user SID in string form ("S-1-5-21-…"), or "" on failure.
std::wstring currentUserSid() {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    std::wstring sid;
    DWORD len = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &len);
    std::vector<BYTE> buf(len);
    if (len && ::GetTokenInformation(token, TokenUser, buf.data(), len, &len)) {
        LPWSTR str = nullptr;
        if (::ConvertSidToStringSidW(
                reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &str)) {
            sid = str;
            ::LocalFree(str);
        }
    }
    ::CloseHandle(token);
    return sid;
}

// Create the single-instance, local-only pipe the helper connects back to. Read/
// write is granted to the current user and to BUILTIN\Administrators (so an
// over-the-shoulder elevation, which runs the helper as a different admin
// account, can still connect); nobody else. Returns INVALID_HANDLE_VALUE on error.
HANDLE createHelperPipe(const std::wstring& name) {
    const std::wstring user = currentUserSid();
    if (user.empty()) return INVALID_HANDLE_VALUE;
    // D:P = protected DACL (nothing inherited); GRGW = generic read + write.
    const std::wstring sddl =
        L"D:P(A;;GRGW;;;" + user + L")(A;;GRGW;;;BA)";
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    HANDLE h = ::CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        /*nMaxInstances=*/1, /*out=*/64 * 1024, /*in=*/64 * 1024,
        /*timeout=*/0, &sa);
    ::LocalFree(sd);
    return h;
}

}  // namespace

// The listening pipe and its overlapped connect. The OVERLAPPED must stay put while
// a connect is outstanding, hence a heap-allocated, non-moving struct.
struct UacHelperSession::Pipe {
    HANDLE handle = INVALID_HANDLE_VALUE;  // INVALID once handed to the socket
    HANDLE event = nullptr;                // manual-reset; signaled on connect
    OVERLAPPED ov{};
    bool pending = false;                  // a ConnectNamedPipe is outstanding

    Pipe() : event(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    ~Pipe() {
        if (handle != INVALID_HANDLE_VALUE) {
            if (pending) {
                ::CancelIoEx(handle, &ov);
                DWORD n = 0;
                ::GetOverlappedResult(handle, &ov, &n, TRUE);  // ov must outlive it
            }
            ::CloseHandle(handle);
        }
        if (event) ::CloseHandle(event);
    }
};

UacHelperSession::UacHelperSession(QObject* parent) : IHelperSession(parent) {}

UacHelperSession::~UacHelperSession() {
    // Disable the notifiers before closing the handles they wait on.
    if (connectNotifier_) connectNotifier_->setEnabled(false);
    if (exitNotifier_) exitNotifier_->setEnabled(false);
    pipe_.reset();
    closeProcessHandle();
}

void UacHelperSession::closeProcessHandle() {
    if (proc_) {
        ::CloseHandle(static_cast<HANDLE>(proc_));
        proc_ = nullptr;
    }
}

void UacHelperSession::reset() {
    // Notifiers first: they must stop waiting before their handles close.
    if (connectNotifier_) {
        connectNotifier_->setEnabled(false);
        connectNotifier_->deleteLater();
        connectNotifier_ = nullptr;
    }
    if (exitNotifier_) {
        exitNotifier_->setEnabled(false);
        exitNotifier_->deleteLater();
        exitNotifier_ = nullptr;
    }
    if (sock_) {
        sock_->abort();
        sock_->deleteLater();
        sock_ = nullptr;
    }
    pipe_.reset();
    closeProcessHandle();
    helperPid_ = 0;
    running_ = false;
    pendingCancel_ = false;
}

void UacHelperSession::start(const QString& helperPath,
                             const QStringList& verbArgs) {
    reset();  // the session is reused across runs
    done_ = false;

    // 1) Create the pipe before launching, so it exists the instant the helper
    //    starts. Listening (ConnectNamedPipe) is deferred until the PID is known.
    const QString pipeName =
        QStringLiteral("\\\\.\\pipe\\kuiper-imager-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    pipe_ = std::make_unique<Pipe>();
    pipe_->handle = pipe_->event ? createHelperPipe(toW(pipeName))
                                 : INVALID_HANDLE_VALUE;
    if (pipe_->handle == INVALID_HANDLE_VALUE) {
        pipe_.reset();
        finishOnce(-1, Outcome::LaunchFailed);
        return;
    }

    // 2) Build the command line: --pipe <name> then the verb args.
    QStringList fullArgs;
    fullArgs << QStringLiteral("--pipe") << pipeName << verbArgs;
    std::wstring params;
    for (int i = 0; i < fullArgs.size(); ++i) {
        if (i) params.push_back(L' ');
        params += quoteArg(toW(fullArgs.at(i)));
    }
    const std::wstring helperW = toW(helperPath);

    // 3) Launch. Normally elevate via "runas" (UAC). KUIPER_HELPER_NO_ELEVATE
    //    launches directly (valid only when already elevated) — still over the pipe.
    HANDLE hProc = nullptr;
    if (qEnvironmentVariableIsSet("KUIPER_HELPER_NO_ELEVATE")) {
        std::wstring cmd = quoteArg(helperW) + L" " + params;
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!::CreateProcessW(helperW.c_str(), cmdBuf.data(), nullptr, nullptr,
                              FALSE, 0, nullptr, nullptr, &si, &pi)) {
            const DWORD e = ::GetLastError();
            pipe_.reset();
            finishOnce(-1, e == ERROR_FILE_NOT_FOUND ? Outcome::HelperNotFound
                                                     : Outcome::LaunchFailed);
            return;
        }
        ::CloseHandle(pi.hThread);
        hProc = pi.hProcess;
        helperPid_ = pi.dwProcessId;
    } else {
        // ShellExecuteEx("runas") needs COM initialised on this (the GUI) thread.
        // QApplication already OleInitialize()s the main thread, and start() is
        // only ever called from it, so we rely on that rather than add an Ole32
        // dependency and the matching CoUninitialize bookkeeping.
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = L"runas";
        sei.lpFile = helperW.c_str();
        sei.lpParameters = params.c_str();
        sei.nShow = SW_HIDE;
        if (!::ShellExecuteExW(&sei) || !sei.hProcess) {
            const DWORD e = ::GetLastError();
            Outcome o = e == ERROR_CANCELLED      ? Outcome::AuthCancelled
                        : e == ERROR_FILE_NOT_FOUND ? Outcome::HelperNotFound
                                                    : Outcome::LaunchFailed;
            pipe_.reset();
            finishOnce(-1, o);
            return;
        }
        hProc = sei.hProcess;
        helperPid_ = ::GetProcessId(hProc);
    }

    proc_ = hProc;
    running_ = true;

    // 4) A process handle becomes signaled on exit — the event-loop-friendly analog
    //    of QProcess::finished.
    exitNotifier_ = new QWinEventNotifier(hProc, this);
    connect(exitNotifier_, &QWinEventNotifier::activated, this,
            [this] { onProcessExit(); });
    exitNotifier_->setEnabled(true);

    // 5) Only now listen. helperPid_ is known, so the identity check can never run
    //    against an unknown PID — ShellExecuteEx may pump messages while the UAC
    //    prompt is up, so listening earlier could re-enter us mid-launch. A helper
    //    that already connected is picked up as ERROR_PIPE_CONNECTED. If listening
    //    fails, the helper can't connect, refuses to write, and exits with
    //    ipc::kExitNoChannel, which HelperProcess reports.
    connectNotifier_ = new QWinEventNotifier(pipe_->event, this);
    connectNotifier_->setEnabled(false);  // armed by armConnect() when pending
    connect(connectNotifier_, &QWinEventNotifier::activated, this,
            [this] { completeConnect(); });
    armConnect();
}

bool UacHelperSession::armConnect() {
    Pipe& p = *pipe_;
    p.ov = OVERLAPPED{};
    p.ov.hEvent = p.event;
    ::ResetEvent(p.event);
    if (!::ConnectNamedPipe(p.handle, &p.ov)) {
        switch (::GetLastError()) {
            case ERROR_IO_PENDING:  // normal: wait for the helper
                p.pending = true;
                connectNotifier_->setEnabled(true);
                return true;
            case ERROR_PIPE_CONNECTED:  // the helper connected before we listened
            case ERROR_NO_DATA:         // ... and already closed its end
                break;
            default:
                return false;
        }
    }
    acceptClient();
    return true;
}

void UacHelperSession::completeConnect() {
    if (!pipe_ || !pipe_->pending) return;
    DWORD n = 0;
    if (!::GetOverlappedResult(pipe_->handle, &pipe_->ov, &n, FALSE)) {
        const DWORD e = ::GetLastError();
        if (e == ERROR_IO_INCOMPLETE) return;  // still waiting for a client
        pipe_->pending = false;
        connectNotifier_->setEnabled(false);
        if (e != ERROR_PIPE_CONNECTED && e != ERROR_NO_DATA) return;  // see start()
    } else {
        pipe_->pending = false;
        connectNotifier_->setEnabled(false);
    }
    acceptClient();
}

void UacHelperSession::acceptClient() {
    // Identity check, failing closed: the client must provably be the helper we
    // launched. The DACL already limits who can connect; this pins it to one PID.
    ULONG clientPid = 0;
    const bool ours = helperPid_ != 0 &&
                      ::GetNamedPipeClientProcessId(pipe_->handle, &clientPid) &&
                      clientPid == helperPid_;
    if (!ours) {
        // Drop it and listen again; the real helper retries a busy pipe.
        ::DisconnectNamedPipe(pipe_->handle);
        armConnect();
        return;
    }

    auto* s = new QLocalSocket(this);
    if (!s->setSocketDescriptor(reinterpret_cast<qintptr>(pipe_->handle))) {
        delete s;
        return;  // the helper exits without a result; reported on exit
    }
    pipe_->handle = INVALID_HANDLE_VALUE;  // owned by the socket from here on
    sock_ = s;
    connect(sock_, &QLocalSocket::readyRead, this,
            [this] { emit bytesReceived(sock_->readAll()); });
    if (pendingCancel_) {  // cancel was requested before the helper connected
        sock_->write("cancel\n");
        sock_->flush();
        pendingCancel_ = false;
    }
    if (sock_->bytesAvailable()) emit bytesReceived(sock_->readAll());
}

void UacHelperSession::onProcessExit() {
    if (exitNotifier_) exitNotifier_->setEnabled(false);
    running_ = false;

    // The helper may have connected — and written its result — before we got to
    // process the connect. Reap it now, or that final line would be lost.
    if (!sock_) completeConnect();

    // Drain any final lines the helper wrote just before exiting, so the terminal
    // result/error is parsed before we report the process-level outcome. Loop until
    // the peer is gone and nothing is buffered — a single read can miss a result
    // line that the socket surfaces in more than one segment.
    if (sock_) {
        while (sock_->state() == QLocalSocket::ConnectedState &&
               sock_->waitForReadyRead(50)) {
            emit bytesReceived(sock_->readAll());
        }
        if (sock_->bytesAvailable()) emit bytesReceived(sock_->readAll());
    }

    DWORD code = 0;
    if (proc_) ::GetExitCodeProcess(static_cast<HANDLE>(proc_), &code);
    closeProcessHandle();
    if (exitNotifier_) {
        exitNotifier_->deleteLater();
        exitNotifier_ = nullptr;
    }
    // An NTSTATUS error code (0xC0000005 access violation, 0xC0000409 fail-fast,
    // …) as the exit code means the helper died abnormally — the Windows analog
    // of QProcess::CrashExit.
    const bool crashed = (code & 0xC0000000u) == 0xC0000000u;
    finishOnce(static_cast<int>(code), crashed ? Outcome::Crashed : Outcome::Ok);
}

void UacHelperSession::finishOnce(int exitCode, Outcome outcome) {
    if (done_) return;
    done_ = true;
    running_ = false;
    emit finished(exitCode, outcome);
}

void UacHelperSession::writeCancel() {
    if (sock_ && sock_->state() == QLocalSocket::ConnectedState) {
        sock_->write("cancel\n");
        sock_->flush();
    } else {
        pendingCancel_ = true;  // helper not connected yet; flush on connect
    }
}

bool UacHelperSession::running() const { return running_; }

IHelperSession* makeHelperSession(QObject* parent) {
    return new UacHelperSession(parent);
}
