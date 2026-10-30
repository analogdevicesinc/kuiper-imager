// O_DIRECT is a GNU extension; request it before any libc header is pulled in.
// The build may already define _GNU_SOURCE globally, so guard against a redefine.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/swap.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <span>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "kuiper/platform/IDriveBackend.hpp"
#include "platform/common/AlignedRawDevice.hpp"
#include "platform/common/BlockIo.hpp"

namespace kuiper {
namespace {

constexpr int kOpenRetries = 25;           // ~5 s total across EBUSY retries
constexpr long kRetryNanos = 200'000'000;  // 200 ms

void sleepBetweenRetries() {
    timespec ts{0, kRetryNanos};
    nanosleep(&ts, nullptr);
}

// Trailing integer of a partition node name, e.g. "/dev/mmcblk0p3" => 3,
// "/dev/sdb1" => 1. Zero if the name ends in no digit. This is only a display
// hint (the partition role comes from inspection, never from this number).
int trailingNumber(const QString& node) {
    int i = node.size();
    while (i > 0 && node[i - 1].isDigit()) --i;
    if (i == node.size()) return 0;
    return node.mid(i).toInt();
}

// Build a Partition from one lsblk child object. The node is taken verbatim —
// never composed — which is the whole point of enumerating instead of guessing.
Partition parsePartition(const QJsonObject& obj) {
    Partition p;
    const QString node = obj.value("name").toString();
    p.node = node.toStdString();
    p.number = trailingNumber(node);
    p.sizeBytes = obj.value("size").toVariant().toULongLong();
    p.fsType = obj.value("fstype").toString().toStdString();
    p.label = obj.value("label").toString().toStdString();
    // We request the scalar MOUNTPOINT column (not the newer MOUNTPOINTS array),
    // so lsblk always emits this single key; empty means the partition is not
    // mounted.
    p.mountpoint = obj.value("mountpoint").toString().toStdString();
    return p;
}

// Mountpoints that mean the node holds part of the running OS. See docs:
// platform-backends (system vs removable).
bool isCriticalMount(const QString& mp) {
    if (mp == "/" || mp == "[SWAP]") return true;
    for (const char* root : {"/boot", "/usr", "/var", "/etc"}) {
        const QString r = QLatin1String(root);
        if (mp == r || mp.startsWith(r + '/')) return true;
    }
    return false;
}

// Recursively: does any node in this disk's subtree host the running OS?
// Critical mountpoint, or a MAJ:MIN match against the device backing "/"
// (rootDev, empty to skip). See docs: platform-backends (system vs removable).
bool subtreeIsSystem(const QJsonObject& node, const QString& rootDev) {
    if (isCriticalMount(node.value("mountpoint").toString())) return true;
    if (!rootDev.isEmpty() && node.value("maj:min").toString() == rootDev) {
        return true;
    }
    for (const auto& c : node.value("children").toArray()) {
        if (subtreeIsSystem(c.toObject(), rootDev)) return true;
    }
    return false;
}

// Best-effort "can the user pull this media?" heuristic (advisory; --force
// overrides). See docs: platform-backends (system vs removable).
bool computeRemovable(const QJsonObject& obj) {
    if (obj.value("rm").toBool() || obj.value("hotplug").toBool()) return true;
    const QString tran = obj.value("tran").toString();
    if (tran == "usb") return true;
    if (tran == "mmc") {
        const QString base = obj.value("name").toString().section('/', -1);
        QFile f("/sys/block/" + base + "/device/type");
        if (f.open(QIODevice::ReadOnly)) {
            if (QString::fromLatin1(f.readAll()).trimmed() == "SD") return true;
        }
    }
    return false;
}

// The device backing "/" as "maj:min" (matching lsblk's MAJ:MIN column), or
// empty if it cannot be determined. Feeds subtreeIsSystem().
QString rootDeviceId() {
    struct stat st{};
    if (::stat("/", &st) != 0) return {};
    return QString("%1:%2").arg(major(st.st_dev)).arg(minor(st.st_dev));
}

// Map a POSIX I/O errno onto the domain error vocabulary.
std::unexpected<Error> ioError(int e, const std::string& what) {
    switch (e) {
        case ENOSPC:
            return Err(ErrorCode::DiskFull, what, std::strerror(e));
        case ENXIO:
        case ENODEV:
        case EIO:
            return Err(ErrorCode::DeviceRemoved, what, std::strerror(e));
        case EACCES:
        case EPERM:
            return Err(ErrorCode::PermissionDenied, what,
                       "Run with sudo (root is required for raw device access)");
        case EBUSY:
            return Err(ErrorCode::DeviceBusy, what, std::strerror(e));
        default:
            return Err(ErrorCode::Unknown, what, std::strerror(e));
    }
}

// The current mountpoint of `node` per lsblk, or "" if it is not mounted. Read
// from lsblk (not from udisksctl's human output) so it is stable and locale-proof.
std::string currentMountpoint(const std::string& node) {
    QProcess proc;
    proc.start("lsblk",
               {"-n", "-o", "MOUNTPOINT", QString::fromStdString(node)});
    if (!proc.waitForStarted(3000) || !proc.waitForFinished(5000) ||
        proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed().toStdString();
}

// Mount `node` through udisks2 — the unprivileged path a file manager uses,
// authorized by polkit without root. Returns the mountpoint on success, or "" if
// udisks2 is absent/declined so the caller can fall back to the ::mount(2) syscall.
// --no-user-interaction never blocks on a polkit prompt: if auth is required and
// unavailable it fails fast rather than hanging a non-interactive run.
std::string udisksMount(const std::string& node) {
    QProcess proc;
    proc.start("udisksctl", {"mount", "--no-user-interaction", "-b",
                             QString::fromStdString(node)});
    if (!proc.waitForStarted(3000) || !proc.waitForFinished(15000) ||
        proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return {};
    }
    return currentMountpoint(node);  // authoritative, locale-independent
}

// Best-effort unmount through udisks2 (the inverse of udisksMount). Used by the
// handle that udisksMount produced; failures are harmless (the card can still be
// pulled), so the result is ignored, mirroring the syscall path's umount2.
void udisksUnmount(const std::string& node) {
    QProcess proc;
    proc.start("udisksctl", {"unmount", "--no-user-interaction", "-b",
                             QString::fromStdString(node)});
    if (proc.waitForStarted(3000)) proc.waitForFinished(15000);
}

// One open raw-device session on Linux: owns the fd and does positioned,
// block-aligned I/O with direct syscalls. All alignment/staging bookkeeping lives
// in detail::AlignedRawDevice above this; LinuxBlockIo is the thin BlockIo
// primitive beneath it. The destructor closes the fd. See docs: platform-backends.
class LinuxBlockIo final : public detail::BlockIo {
public:
    explicit LinuxBlockIo(int fd) : fd_(fd) {}

    ~LinuxBlockIo() override {
        if (fd_ >= 0) {
            ::close(fd_);  // do not retry on EINTR (Linux closes the fd anyway)
        }
    }

    LinuxBlockIo(const LinuxBlockIo&) = delete;
    LinuxBlockIo& operator=(const LinuxBlockIo&) = delete;

    // Cache block size + capacity. Called once, right after construction.
    void init() {
        int ssz = 0;
        if (::ioctl(fd_, BLKSSZGET, &ssz) != 0 || ssz <= 0) {
            ssz = 512;  // conservative default
        }
        blockSize_ = static_cast<unsigned>(ssz);

        std::uint64_t bytes = 0;
        devSize_ = (::ioctl(fd_, BLKGETSIZE64, &bytes) == 0) ? bytes : 0;
    }

    unsigned blockSize() const noexcept override { return blockSize_; }
    std::uint64_t deviceSize() const noexcept override { return devSize_; }

    Result<std::size_t> pread(std::byte* buf, std::size_t len,
                              std::uint64_t off) override {
        std::size_t done = 0;
        while (done < len) {
            const ssize_t n = ::pread(fd_, buf + done, len - done,
                                      static_cast<off_t>(off + done));
            if (n < 0) {
                if (errno == EINTR) continue;
                return ioError(errno, "Read from device failed");
            }
            if (n == 0) break;  // EOF
            done += static_cast<std::size_t>(n);
        }
        return done;
    }

    Result<void> pwrite(const std::byte* buf, std::size_t len,
                        std::uint64_t off) override {
        std::size_t done = 0;
        while (done < len) {
            const ssize_t n = ::pwrite(fd_, buf + done, len - done,
                                       static_cast<off_t>(off + done));
            if (n < 0) {
                if (errno == EINTR) continue;
                return ioError(errno, "Write to device failed");
            }
            if (n == 0) {
                return Err(ErrorCode::Unknown, "Write returned zero bytes");
            }
            done += static_cast<std::size_t>(n);
        }
        return {};
    }

    Result<void> sync() override {
        if (::fsync(fd_) != 0) {
            return ioError(errno, "fsync failed");
        }
        ::ioctl(fd_, BLKFLSBUF);  // drop the bdev page cache (best effort)
        return {};
    }

    void dropCache() override {
        // Force a subsequent verify read to hit the media; harmless under
        // O_DIRECT and the belt-and-suspenders path for the buffered fallback.
        ::posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
    }

    void rereadPartTable() override {
        ::ioctl(fd_, BLKRRPART);  // best effort; harmless if it fails
    }

private:
    int fd_ = -1;
    unsigned blockSize_ = 512;
    std::uint64_t devSize_ = 0;
};

class LinuxDriveBackend final : public IDriveBackend {
public:
    Result<DriveList> listDrives() override {
        // -b bytes, -J JSON, -p full paths. No -d: the children[] tree carries
        // both the partitions and the holders (LVM/LUKS/RAID) the system check
        // walks. Columns are only what we consume. See docs: platform-backends.
        QProcess proc;
        proc.start("lsblk",
                   {"-b", "-J", "-p", "-o",
                    "NAME,SIZE,MODEL,RM,HOTPLUG,TRAN,TYPE,RO,FSTYPE,LABEL,"
                    "MOUNTPOINT,MAJ:MIN"});
        if (!proc.waitForStarted(3000)) {
            return Err(ErrorCode::Unknown,
                       "Failed to start 'lsblk'",
                       "Is util-linux installed?");
        }
        if (!proc.waitForFinished(5000)) {
            return Err(ErrorCode::Unknown, "'lsblk' did not finish in time");
        }
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            return Err(ErrorCode::Unknown, "'lsblk' failed",
                       proc.readAllStandardError().toStdString());
        }

        QJsonParseError parseErr;
        const auto doc =
            QJsonDocument::fromJson(proc.readAllStandardOutput(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            return Err(ErrorCode::Unknown, "Could not parse lsblk JSON",
                       parseErr.errorString().toStdString());
        }

        const QString rootDev = rootDeviceId();  // "maj:min" of the disk under /

        DriveList drives;
        for (const auto& v : doc.object().value("blockdevices").toArray()) {
            const auto obj = v.toObject();

            // Only whole disks are flash candidates; skip virtual/optical nodes.
            if (obj.value("type").toString() != "disk") continue;
            const auto name = obj.value("name").toString();
            if (name.startsWith("/dev/loop") || name.startsWith("/dev/ram") ||
                name.startsWith("/dev/zram") || name.startsWith("/dev/sr")) {
                continue;
            }

            Drive drive;
            drive.node = name.toStdString();
            drive.description =
                obj.value("model").toString().trimmed().toStdString();
            drive.sizeBytes = obj.value("size").toVariant().toULongLong();
            drive.isRemovable = computeRemovable(obj);
            drive.isSystem = subtreeIsSystem(obj, rootDev);

            // Top-level partitions only. Kuiper cards use a simple partition
            // table (no extended/logical partitions), so we do not recurse into
            // nested children — which would also pull in LVM/LUKS/RAID mapper
            // nodes that are not partitions of this drive.
            for (const auto& c : obj.value("children").toArray()) {
                drive.partitions.push_back(parsePartition(c.toObject()));
            }
            drives.push_back(std::move(drive));
        }
        return drives;
    }

    Result<MountedPartition> mount(const Partition& partition) override {
        // Already mounted (e.g. desktop auto-mount): borrow it, don't touch it.
        if (!partition.mountpoint.empty()) {
            return MountedPartition(partition.mountpoint, {});
        }
        // Nothing to mount without a filesystem — the caller picked the wrong
        // partition (e.g. the raw bootloader slot).
        if (partition.fsType.empty()) {
            return Err(ErrorCode::NotFound,
                       "Partition has no filesystem to mount: " + partition.node,
                       "Only a formatted partition (vfat/ext4) can be mounted.");
        }

        // Prefer udisks2 (the unprivileged, polkit-authorized path a file manager
        // uses) so list-projects / configure work right after a flash without sudo:
        // a freshly written card is present but not auto-mounted, and mounting it
        // ourselves with ::mount(2) needs root. The handle unmounts via udisks2 too.
        // When udisks2 isn't on the system (headless / minimal) this returns "" and
        // we fall through to the syscall below, where the caller is root anyway.
        if (auto mp = udisksMount(partition.node); !mp.empty()) {
            const std::string node = partition.node;
            return MountedPartition(std::move(mp),
                                    [node] { udisksUnmount(node); });
        }

        char tmpl[] = "/tmp/kuiper-mnt-XXXXXX";
        const char* dir = ::mkdtemp(tmpl);
        if (!dir) {
            return ioError(errno, "Cannot create mount point for " +
                                      partition.node);
        }
        const std::string mountDir = dir;

        if (::mount(partition.node.c_str(), mountDir.c_str(),
                    partition.fsType.c_str(), 0, nullptr) != 0) {
            const int e = errno;
            ::rmdir(mountDir.c_str());
            return ioError(e, "Cannot mount " + partition.node + " (" +
                                  partition.fsType + ")");
        }

        // Owning handle: unmount and remove the temp dir on scope exit.
        return MountedPartition(mountDir, [mountDir] {
            ::umount2(mountDir.c_str(), 0);
            ::rmdir(mountDir.c_str());
        });
    }

    const char* name() const noexcept override { return "linux"; }

    Result<void> unmountAll(const std::string& node) override {
        // Query the device tree. If lsblk can't read it (e.g. not a block
        // device), there is nothing to unmount — let openForWrite give the
        // definitive error instead.
        QProcess proc;
        proc.start("lsblk",
                   {"-J", "-p", "-o", "NAME,TYPE,MOUNTPOINT",
                    QString::fromStdString(node)});
        if (!proc.waitForStarted(3000) || !proc.waitForFinished(5000) ||
            proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            return {};
        }
        const auto doc =
            QJsonDocument::fromJson(proc.readAllStandardOutput());
        if (!doc.isObject()) return {};

        std::vector<QString> mounts;  // filesystem mountpoints to umount
        std::vector<QString> swaps;   // device paths active as swap
        bool holder = false;          // md/LVM/LUKS stacked on the device

        // Recursively walk the block tree collecting what must be released.
        std::function<void(const QJsonObject&)> walk = [&](const QJsonObject& o) {
            const QString type = o.value("type").toString();
            const QString mp = o.value("mountpoint").toString();
            const QString nm = o.value("name").toString();
            if (type == "crypt" || type == "lvm" || type.startsWith("raid")) {
                holder = true;
            }
            if (mp == "[SWAP]") {
                swaps.push_back(nm);
            } else if (!mp.isEmpty()) {
                mounts.push_back(mp);
            }
            for (const auto& c : o.value("children").toArray()) {
                walk(c.toObject());
            }
        };
        for (const auto& v : doc.object().value("blockdevices").toArray()) {
            walk(v.toObject());
        }

        if (holder) {
            return Err(ErrorCode::DeviceBusy,
                       "Device is in use by LVM/RAID/LUKS: " + node,
                       "Deactivate the volume group / array first");
        }

        // Turn off swap, then unmount filesystems (deepest last-discovered
        // first). Non-lazy: a genuinely busy mount is caught by O_EXCL later.
        for (const auto& s : swaps) {
            ::swapoff(s.toUtf8().constData());  // best effort
        }
        for (auto it = mounts.rbegin(); it != mounts.rend(); ++it) {
            if (::umount2(it->toUtf8().constData(), 0) != 0) {
                if (errno == EINVAL || errno == ENOENT) continue;  // not mounted
                // EBUSY and friends: leave to the O_EXCL retry loop.
            }
        }
        return {};
    }

    Result<std::unique_ptr<IRawDevice>> openForWrite(
        const std::string& node) override {
        bool tryDirect = true;
        for (int attempt = 0; attempt < kOpenRetries; ++attempt) {
            const int flags =
                O_RDWR | O_EXCL | O_CLOEXEC | (tryDirect ? O_DIRECT : 0);
            int fd = ::open(node.c_str(), flags);
            if (fd >= 0) {
                struct stat st{};
                if (::fstat(fd, &st) != 0 || !S_ISBLK(st.st_mode)) {
                    ::close(fd);
                    return Err(ErrorCode::InvalidImage,
                               "Not a block device: " + node,
                               "--drive must point at a disk like /dev/sdX");
                }
                return makeDevice(fd);
            }
            // O_DIRECT unsupported on this target (e.g. some file-backed loops):
            // fall back to buffered and retry this attempt.
            if (errno == EINVAL && tryDirect) {
                tryDirect = false;
                --attempt;
                continue;
            }
            if (errno == EBUSY) {
                unmountAll(node);  // re-race the udev auto-remount, then retry
                sleepBetweenRetries();
                continue;
            }
            return ioError(errno, "Cannot open device for writing: " + node);
        }
        return Err(ErrorCode::DeviceBusy,
                   "Device stayed busy: " + node,
                   "Close programs using it, or unmount it manually");
    }

    Result<std::unique_ptr<IRawDevice>> openForRead(
        const std::string& node) override {
        bool tryDirect = true;
        for (int attempt = 0; attempt < kOpenRetries; ++attempt) {
            const int flags =
                O_RDONLY | O_EXCL | O_CLOEXEC | (tryDirect ? O_DIRECT : 0);
            int fd = ::open(node.c_str(), flags);
            if (fd >= 0) {
                return makeDevice(fd);
            }
            if (errno == EINVAL && tryDirect) {
                tryDirect = false;
                --attempt;
                continue;
            }
            if (errno == EBUSY) {
                sleepBetweenRetries();
                continue;
            }
            return ioError(errno, "Cannot open device for reading: " + node);
        }
        return Err(ErrorCode::DeviceBusy, "Device stayed busy for verify: " + node);
    }

private:
    // Wrap an open fd in a LinuxBlockIo and the shared AlignedRawDevice engine.
    // Takes ownership of the fd on success; closes it on an init failure (OOM).
    static Result<std::unique_ptr<IRawDevice>> makeDevice(int fd) {
        auto io = std::make_unique<LinuxBlockIo>(fd);
        io->init();  // cache geometry (fd owned by io from here)
        auto dev = std::make_unique<detail::AlignedRawDevice>(std::move(io));
        if (auto r = dev->init(); !r) return std::unexpected(r.error());
        return std::move(dev);
    }
};

}  // namespace

std::unique_ptr<IDriveBackend> makeDriveBackend() {
    return std::make_unique<LinuxDriveBackend>();
}

}  // namespace kuiper
