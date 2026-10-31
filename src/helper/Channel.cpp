#include "Channel.hpp"

#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace kuiper::helper {
namespace {

// stdio transport: the original Linux/macOS path. NDJSON on stdout, "cancel" on
// stdin. Line-flushed so the GUI sees progress as it arrives.
class StdioChannel final : public Channel {
public:
    void writeLine(const std::string& s) override {
        std::fwrite(s.data(), 1, s.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
    void watchCancel(std::atomic<bool>& flag) override {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "cancel") break;
        }
        flag.store(true);  // "cancel", or EOF: the GUI is gone
    }
};

#ifdef _WIN32

// Named-pipe transport: the GUI (unprivileged) is the server, this helper the
// client. One duplex pipe carries NDJSON out and "cancel" in. The handle is opened
// OVERLAPPED and each call drives its own OVERLAPPED: a *synchronous* handle would
// serialize all I/O behind the file-object lock, so the cancel watcher's pending
// blocking ReadFile would stall every progress WriteFile — freezing the write.
// Overlapped I/O (one event per direction) lets read and write proceed at once.
class PipeChannel final : public Channel {
public:
    explicit PipeChannel(HANDLE h)
        : h_(h),
          writeEvent_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          readEvent_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~PipeChannel() override {
        if (writeEvent_) ::CloseHandle(writeEvent_);
        if (readEvent_) ::CloseHandle(readEvent_);
        if (h_ != INVALID_HANDLE_VALUE) ::CloseHandle(h_);
    }

    void writeLine(const std::string& s) override {
        std::string buf = s;
        buf.push_back('\n');
        std::size_t off = 0;
        while (off < buf.size()) {
            OVERLAPPED ov{};
            ov.hEvent = writeEvent_;
            ::ResetEvent(writeEvent_);
            const DWORD want = static_cast<DWORD>(buf.size() - off);
            if (!::WriteFile(h_, buf.data() + off, want, nullptr, &ov) &&
                ::GetLastError() != ERROR_IO_PENDING) {
                return;  // GUI gone; nothing we can do from here
            }
            DWORD wrote = 0;
            if (!::GetOverlappedResult(h_, &ov, &wrote, TRUE) || wrote == 0) {
                return;
            }
            off += wrote;
        }
        ::FlushFileBuffers(h_);
    }

    void watchCancel(std::atomic<bool>& flag) override {
        std::string acc;
        char chunk[256];
        for (;;) {
            OVERLAPPED ov{};
            ov.hEvent = readEvent_;
            ::ResetEvent(readEvent_);
            if (!::ReadFile(h_, chunk, sizeof(chunk), nullptr, &ov) &&
                ::GetLastError() != ERROR_IO_PENDING) {
                flag.store(true);  // pipe broken / EOF: the GUI is gone
                return;
            }
            DWORD got = 0;
            if (!::GetOverlappedResult(h_, &ov, &got, TRUE) || got == 0) {
                flag.store(true);
                return;
            }
            acc.append(chunk, got);
            std::size_t nl;
            while ((nl = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, nl);
                acc.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line == "cancel") {
                    flag.store(true);
                    return;
                }
            }
        }
    }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
    HANDLE writeEvent_ = nullptr;
    HANDLE readEvent_ = nullptr;
};

// Connect to the GUI's already-listening pipe. It exists before we launch, so this
// normally succeeds at once; retry briefly on the two transient errors that can
// occur in the launch/connect window.
HANDLE connectPipe(const std::string& name) {
    const std::wstring w(name.begin(), name.end());
    for (int attempt = 0; attempt < 10; ++attempt) {
        HANDLE h = ::CreateFileW(w.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                                 nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
        const DWORD e = ::GetLastError();
        if (e == ERROR_PIPE_BUSY) {
            ::WaitNamedPipeW(w.c_str(), 2000);
        } else if (e == ERROR_FILE_NOT_FOUND) {
            ::Sleep(50);  // server not listening yet; brief backoff
        } else {
            break;
        }
    }
    return INVALID_HANDLE_VALUE;
}

#endif  // _WIN32

}  // namespace

std::unique_ptr<Channel> makeChannel(const std::string& pipeName) {
    if (pipeName.empty()) return std::make_unique<StdioChannel>();
#ifdef _WIN32
    HANDLE h = connectPipe(pipeName);
    if (h != INVALID_HANDLE_VALUE) return std::make_unique<PipeChannel>(h);
#endif
    // A pipe was requested but can't be reached (or isn't supported here). Never
    // fall back to stdio: a WIN32-subsystem helper has none, so the write would
    // run with no one to report to or cancel it.
    return nullptr;
}

}  // namespace kuiper::helper
