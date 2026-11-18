#include "HelperProcess.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <future>
#include <memory>
#include <utility>

#include "kuiper/Error.hpp"
#include "kuiper/ipc/HelperProtocol.hpp"

namespace {

// Platform-correct helper filename (the Windows helper is a .exe).
QString helperFileName() {
#ifdef Q_OS_WIN
    return QStringLiteral("kuiper-imager-helper.exe");
#else
    return QStringLiteral("kuiper-imager-helper");
#endif
}

}  // namespace

HelperProcess::HelperProcess(QObject* parent)
    : QObject(parent), session_(makeHelperSession(this)) {
    connect(session_, &IHelperSession::bytesReceived, this,
            [this](const QByteArray& chunk) { feed(chunk); });
    connect(session_, &IHelperSession::finished, this,
            [this](int code, IHelperSession::Outcome outcome) {
                onFinished(code, outcome);
            });
}

QString HelperProcess::resolveHelperPath() {
    const QString name = helperFileName();
    // 1) explicit override (tests / packaging quirks)
    if (qEnvironmentVariableIsSet("KUIPER_HELPER")) {
        const QString p = qEnvironmentVariable("KUIPER_HELPER");
        if (!p.isEmpty()) return p;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    // 2) build tree: gui at build/src/gui, helper at build/src/helper
    const QString devPath = QDir(appDir).filePath("../helper/" + name);
    if (QFileInfo::exists(devPath)) return QDir::cleanPath(devPath);
    // 3) colocated (same dir) — the installed location on Windows
    const QString sibling = QDir(appDir).filePath(name);
    if (QFileInfo::exists(sibling)) return sibling;
#ifdef KUIPER_HELPER_PATH
    // 4) Unix installed location (compiled in; the libexec path)
    return QStringLiteral(KUIPER_HELPER_PATH);
#else
    // Windows has no other location; launching the missing sibling reports
    // HelperNotFound.
    return sibling;
#endif
}

void HelperProcess::start(Verb verb, const QStringList& verbArgs) {
    verb_ = verb;
    result_.reset();
    customizeResult_.reset();
    buf_.clear();
    sawTerminal_ = false;
    session_->start(resolveHelperPath(), verbArgs);
}

void HelperProcess::runFlash(const QString& driveNode, const QString& imagePath,
                             bool verify) {
    QStringList args{"flash", "--image", imagePath, "--drive", driveNode};
    if (!verify) args << "--no-verify";
    start(Verb::Flash, args);
}

void HelperProcess::runWritePreloader(const QString& partitionNode,
                                      const QString& imagePath) {
    start(Verb::Preloader,
          QStringList{"write-preloader", "--partition", partitionNode, "--image",
                      imagePath});
}

void HelperProcess::runCustomize(const QString& imagePath,
                                 const QStringList& featureIds,
                                 std::optional<quint64> growBytes, bool noGrow,
                                 const QString& archOverride,
                                 const QString& scriptPath) {
    QStringList args{"customize", "--image", imagePath};
    for (const QString& id : featureIds) args << "--feature" << id;
    if (noGrow)
        args << "--no-grow";
    else if (growBytes)
        args << "--grow" << QString::number(*growBytes);
    if (!archOverride.isEmpty()) args << "--arch" << archOverride;
    if (!scriptPath.isEmpty()) args << "--script" << scriptPath;
    start(Verb::Customize, args);
}

void HelperProcess::cancel() {
    if (session_->running()) session_->writeCancel();
}

bool HelperProcess::busy() const {
    return session_->running();
}

void HelperProcess::feed(const QByteArray& chunk) {
    buf_.append(chunk);
    int nl;
    while ((nl = buf_.indexOf('\n')) >= 0) {
        const std::string line = buf_.left(nl).toStdString();
        buf_.remove(0, nl + 1);
        const auto msg = kuiper::ipc::parseLine(line);
        switch (msg.type) {
            case kuiper::ipc::HelperMessage::Type::Progress:
                emit progress(msg.progress);
                break;
            case kuiper::ipc::HelperMessage::Type::Result: {
                kuiper::FlashSummary s;
                s.bytesWritten = msg.bytesWritten;
                s.sha256 = msg.sha256;
                result_ = s;
                sawTerminal_ = true;
                break;
            }
            case kuiper::ipc::HelperMessage::Type::CustomizeResult:
                customizeResult_ = msg.customize;
                sawTerminal_ = true;
                break;
            case kuiper::ipc::HelperMessage::Type::Error:
                setError(msg.error);
                sawTerminal_ = true;
                break;
            case kuiper::ipc::HelperMessage::Type::Unknown:
                break;  // stray/non-JSON (e.g. pkexec chatter) — ignore
        }
    }
}

void HelperProcess::setError(const kuiper::Error& e) {
    // Route a terminal error / backstop to the holder the caller will read, so a
    // customize failure doesn't masquerade as a flash result (and vice-versa).
    if (verb_ == Verb::Customize)
        customizeResult_ = std::unexpected(e);
    else
        result_ = std::unexpected(e);
}

void HelperProcess::onFinished(int exitCode, IHelperSession::Outcome outcome) {
    // If the helper produced a terminal line, that is the authoritative result;
    // the process-level outcome is only a backstop for when it did not.
    if (!sawTerminal_) {
        using Outcome = IHelperSession::Outcome;
        if (outcome == Outcome::Ok && exitCode == kuiper::ipc::kExitNoChannel) {
            setError(kuiper::Err(
                kuiper::ErrorCode::Unknown,
                "The helper could not connect back to Kuiper Imager",
                "It was started but could not report progress, so it did "
                "nothing.").error());
            emit finished();
            return;
        }
        switch (outcome) {
            case Outcome::AuthCancelled:
                setError(kuiper::Err(
                    kuiper::ErrorCode::PermissionDenied,
                    "Authentication was cancelled",
                    "Elevated access is required for this operation.").error());
                break;
            case Outcome::HelperNotFound:
                setError(kuiper::Err(
                    kuiper::ErrorCode::NotFound, "The privileged helper was not found",
                    "kuiper-imager-helper is missing; reinstall Kuiper Imager.")
                             .error());
                break;
            case Outcome::Crashed:
                setError(kuiper::Err(kuiper::ErrorCode::Unknown,
                                     "The privileged helper crashed",
                                     "The elevated helper exited abnormally.")
                             .error());
                break;
            case Outcome::LaunchFailed:
                setError(kuiper::Err(
                    kuiper::ErrorCode::Unknown,
                    "The privileged helper could not be launched",
                    "The elevation tool (pkexec on Linux, UAC on Windows) failed "
                    "to start it.").error());
                break;
            case Outcome::Ok:
                setError(kuiper::Err(kuiper::ErrorCode::Unknown,
                                     "The privileged helper failed",
                                     "It exited without reporting a result.")
                             .error());
                break;
        }
    }
    emit finished();
}

kuiper::PreloaderSink makeElevatedPreloaderSink(QObject* context,
                                                std::string partitionNode) {
    return [context = QPointer<QObject>(context),
            node = std::move(partitionNode)](
               const std::string& preloader,
               kuiper::ProgressFn prog) -> kuiper::Result<void> {
        Q_ASSERT(context && QThread::currentThread() != context->thread());
        using Outcome = kuiper::Result<kuiper::FlashSummary>;
        auto done = std::make_shared<std::promise<Outcome>>();
        std::future<Outcome> fut = done->get_future();

        // GUI thread: a one-shot HelperProcess, deleted once it reports.
        const bool posted = context && QMetaObject::invokeMethod(
            context.data(),
            [node, preloader, prog, done] {
                auto* helper = new HelperProcess();
                QObject::connect(helper, &HelperProcess::progress, helper,
                                 [prog](const kuiper::Progress& p) {
                                     if (prog) prog(p);
                                 });
                QObject::connect(helper, &HelperProcess::finished, helper,
                                 [helper, done] {
                                     done->set_value(*helper->result());
                                     helper->deleteLater();
                                 });
                helper->runWritePreloader(QString::fromStdString(node),
                                          QString::fromStdString(preloader));
            },
            Qt::QueuedConnection);

        // If the posted call is dropped (context destroyed first), the promise
        // dies unfulfilled and get() throws broken_promise; never let that
        // escape into ConfigurationService.
        Outcome r = kuiper::Err(kuiper::ErrorCode::Unknown,
                                "The preloader write could not be started");
        if (posted) {
            try {
                r = fut.get();
            } catch (const std::future_error&) {
            }
        }
        if (!r) return std::unexpected(r.error());
        return {};
    };
}
