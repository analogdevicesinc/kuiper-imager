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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "kuiper/platform/IDriveBackend.hpp"

namespace kuiper {
namespace {

constexpr int kOpenRetries = 25;           // ~5 s total across EBUSY retries
constexpr long kRetryNanos = 200'000'000;  // 200 ms
constexpr std::uint64_t kMiB = 1u << 20;
constexpr std::uint64_t kWipeHead = 4 * kMiB;  // clobber MBR/GPT + first parts
constexpr std::uint64_t kWipeTail = 1 * kMiB;  // clobber backup GPT
constexpr std::size_t kAlignBuf = 4u << 20;    // aligned staging / bounce (4 MiB)

void sleepBetweenRetries() {
    timespec ts{0, kRetryNanos};
    nanosleep(&ts, nullptr);
}

// Result of a short util-linux invocation (findmnt/lsblk) for topology queries.
struct CmdResult {
    bool ran = false;  // false => process failed to start/finish
    int code = 1;
    QString out;  // trimmed stdout
    QString err;  // trimmed stderr
};

// Run a util-linux tool and capture its output. Read-only; no device access.
CmdResult runTool(const QString& program, const QStringList& args) {
    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForStarted(3000) || !proc.waitForFinished(5000) ||
        proc.exitStatus() != QProcess::NormalExit) {
        return {};
    }
    CmdResult r;
    r.ran = true;
    r.code = proc.exitCode();
    r.out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    r.err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    return r;
}

// First non-empty line of a tool's output (findmnt/lsblk emit one row here).
QString firstLine(const QString& s) { return s.split('\n').first().trimmed(); }

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

// Owns a page/block-aligned heap buffer (posix_memalign). O_DIRECT requires the
// buffer, the file offset, and the transfer length to be block-aligned; this
// gives us the buffer half. Move-only; frees on scope exit.
class AlignedBuf {
public:
    AlignedBuf() = default;
    AlignedBuf(std::size_t size, std::size_t align) {
        void* p = nullptr;
        if (::posix_memalign(&p, align, size) == 0) {
            p_ = static_cast<std::byte*>(p);
            n_ = size;
        }
    }
    ~AlignedBuf() { std::free(p_); }
    AlignedBuf(AlignedBuf&& o) noexcept : p_(o.p_), n_(o.n_) {
        o.p_ = nullptr;
        o.n_ = 0;
    }
    AlignedBuf& operator=(AlignedBuf&& o) noexcept {
        if (this != &o) {
            std::free(p_);
            p_ = o.p_;
            n_ = o.n_;
            o.p_ = nullptr;
            o.n_ = 0;
        }
        return *this;
    }
    AlignedBuf(const AlignedBuf&) = delete;
    AlignedBuf& operator=(const AlignedBuf&) = delete;

    std::byte* get() const noexcept { return p_; }
    std::size_t size() const noexcept { return n_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

private:
    std::byte* p_ = nullptr;
    std::size_t n_ = 0;
};

class LinuxDriveBackend final : public IDriveBackend {
public:
    ~LinuxDriveBackend() override { close(); }

    Result<DriveList> listDrives() override {
        // -b bytes, -J JSON, -p full paths. No -d: we want the children[] tree
        // so each drive carries its partitions. -o restricts columns to what we
        // consume (FSTYPE/LABEL/MOUNTPOINT are read from the partition rows).
        QProcess proc;
        proc.start("lsblk", {"-b", "-J", "-p", "-o",
                             "NAME,SIZE,MODEL,RM,TYPE,RO,FSTYPE,LABEL,MOUNTPOINT"});
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
            drive.isRemovable = obj.value("rm").toBool();
            drive.isSystem = !drive.isRemovable;

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

    Result<void> openForWrite(const std::string& node) override {
        close();
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
                fd_ = fd;
                direct_ = tryDirect;
                if (auto r = initGeometry(); !r) {
                    close();
                    return r;
                }
                return {};
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

    Result<void> openForRead(const std::string& node) override {
        close();
        bool tryDirect = true;
        for (int attempt = 0; attempt < kOpenRetries; ++attempt) {
            const int flags =
                O_RDONLY | O_EXCL | O_CLOEXEC | (tryDirect ? O_DIRECT : 0);
            int fd = ::open(node.c_str(), flags);
            if (fd >= 0) {
                fd_ = fd;
                direct_ = tryDirect;
                if (auto r = initGeometry(); !r) {
                    close();
                    return r;
                }
                // Without O_DIRECT, drop cached pages so reads hit the media.
                if (!direct_) ::posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
                return {};
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

    void close() override {
        if (fd_ >= 0) {
            ::close(fd_);  // do not retry on EINTR (Linux closes the fd anyway)
            fd_ = -1;
        }
        offset_ = 0;
        wfill_ = 0;
        rpos_ = 0;
        rlen_ = 0;
        devSize_ = 0;
        direct_ = false;
        wbuf_ = AlignedBuf{};
        rbuf_ = AlignedBuf{};
    }

    Result<void> seek(std::uint64_t offset) override {
        if (auto r = flushTail(); !r) return r;  // commit any staged write bytes
        rpos_ = 0;                               // discard read-ahead
        rlen_ = 0;
        offset_ = static_cast<off_t>(offset);
        return {};
    }

    Result<void> write(std::span<const std::byte> data) override {
        const std::byte* p = data.data();
        std::size_t left = data.size();
        while (left > 0) {
            const std::size_t space = kAlignBuf - wfill_;
            const std::size_t n = std::min(left, space);
            std::memcpy(wbuf_.get() + wfill_, p, n);
            wfill_ += n;
            p += n;
            left -= n;
            if (wfill_ == kAlignBuf) {
                if (auto r = flushFull(); !r) return r;
            }
        }
        return {};
    }

    Result<std::size_t> read(std::span<std::byte> buffer) override {
        std::byte* out = buffer.data();
        const std::size_t want = buffer.size();
        std::size_t total = 0;
        while (total < want) {
            if (rlen_ == 0) {
                if (devSize_ != 0 &&
                    static_cast<std::uint64_t>(offset_) >= devSize_) {
                    break;  // EOF
                }
                const std::uint64_t avail =
                    devSize_ ? devSize_ - static_cast<std::uint64_t>(offset_)
                             : kAlignBuf;
                const std::size_t toRead = static_cast<std::size_t>(
                    std::min<std::uint64_t>(kAlignBuf, avail));
                auto n = preadAligned(rbuf_.get(), toRead, offset_);
                if (!n) return std::unexpected(n.error());
                if (*n == 0) break;  // EOF
                offset_ += static_cast<off_t>(*n);
                rpos_ = 0;
                rlen_ = *n;
            }
            const std::size_t take = std::min(rlen_, want - total);
            std::memcpy(out + total, rbuf_.get() + rpos_, take);
            rpos_ += take;
            rlen_ -= take;
            total += take;
        }
        return total;
    }

    Result<std::uint64_t> deviceSize() override {
        if (devSize_ != 0) return devSize_;
        std::uint64_t bytes = 0;
        if (::ioctl(fd_, BLKGETSIZE64, &bytes) != 0) {
            return ioError(errno, "Cannot query device size (BLKGETSIZE64)");
        }
        devSize_ = bytes;
        return bytes;
    }

    Result<void> wipeSignatures() override {
        if (devSize_ == 0) {
            if (auto s = deviceSize(); !s) return std::unexpected(s.error());
        }

        AlignedBuf zeros(static_cast<std::size_t>(kMiB), alignment_);
        if (!zeros) {
            return Err(ErrorCode::Unknown, "Out of memory (aligned wipe buffer)");
        }
        std::memset(zeros.get(), 0, static_cast<std::size_t>(kMiB));

        // Ranges are block-aligned (0, kMiB multiples, and devSize_ which the
        // kernel reports as a multiple of the logical block size), so each
        // pwrite stays O_DIRECT-legal.
        auto zeroRange = [&](std::uint64_t begin,
                             std::uint64_t end) -> Result<void> {
            std::uint64_t off = begin;
            while (off < end) {
                const std::size_t chunk = static_cast<std::size_t>(
                    std::min<std::uint64_t>(end - off, kMiB));
                if (auto r = pwriteAll(zeros.get(), chunk,
                                       static_cast<off_t>(off));
                    !r) {
                    return r;
                }
                off += chunk;
            }
            return {};
        };

        const std::uint64_t head = std::min<std::uint64_t>(kWipeHead, devSize_);
        if (auto r = zeroRange(0, head); !r) return r;
        if (devSize_ > kWipeTail) {
            const std::uint64_t tailBegin =
                std::max<std::uint64_t>(head, devSize_ - kWipeTail);
            if (auto r = zeroRange(tailBegin, devSize_); !r) return r;
        }
        ::fdatasync(fd_);  // make sure the wipe reaches media
        return {};
    }

    Result<void> flushAndSync() override {
        if (auto r = flushTail(); !r) return r;  // commit the staged tail first
        if (::fsync(fd_) != 0) {
            return ioError(errno, "fsync failed");
        }
        ::ioctl(fd_, BLKFLSBUF);  // drop the bdev page cache (best effort)
        // Belt-and-suspenders for the buffered fallback: force the verify pass
        // to re-read from media. Harmless (a no-op cost) under O_DIRECT.
        ::posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
        return {};
    }

    Result<void> rereadPartTable() override {
        ::ioctl(fd_, BLKRRPART);  // best effort; harmless if it fails
        return {};
    }

    Result<std::string> parentDisk(const std::string& node) override {
        const auto r = runTool(
            "lsblk", {"-n", "-o", "PKNAME", QString::fromStdString(node)});
        if (!r.ran) {
            return Err(ErrorCode::Unknown, "Failed to run 'lsblk'",
                       "Is util-linux installed?");
        }
        if (r.code != 0) {
            return Err(ErrorCode::NotFound,
                       "Cannot determine parent disk of: " + node,
                       r.err.toStdString());
        }
        const QString pk = firstLine(r.out);
        if (pk.isEmpty()) return std::string{};  // already a whole disk
        return "/dev/" + pk.toStdString();
    }

    Result<std::string> mountpointOf(const std::string& node) override {
        const auto r = runTool("findmnt", {"-n", "-o", "TARGET", "--source",
                                           QString::fromStdString(node)});
        if (!r.ran) {
            return Err(ErrorCode::Unknown, "Failed to run 'findmnt'",
                       "Is util-linux installed?");
        }
        // Exit 1 with empty output means "not mounted" — not an error.
        return firstLine(r.out).toStdString();
    }

private:
    // Cache block size + capacity and allocate the aligned I/O buffers. Called
    // once per successful open.
    Result<void> initGeometry() {
        offset_ = 0;
        wfill_ = 0;
        rpos_ = 0;
        rlen_ = 0;

        int ssz = 0;
        if (::ioctl(fd_, BLKSSZGET, &ssz) != 0 || ssz <= 0) {
            ssz = 512;  // conservative default
        }
        blockSize_ = static_cast<unsigned>(ssz);
        alignment_ = std::max<unsigned>(blockSize_, 4096u);

        std::uint64_t bytes = 0;
        devSize_ = (::ioctl(fd_, BLKGETSIZE64, &bytes) == 0) ? bytes : 0;

        wbuf_ = AlignedBuf(kAlignBuf, alignment_);
        rbuf_ = AlignedBuf(kAlignBuf, alignment_);
        if (!wbuf_ || !rbuf_) {
            return Err(ErrorCode::Unknown, "Out of memory (aligned I/O buffers)");
        }
        return {};
    }

    std::size_t roundUpToBlock(std::size_t x) const {
        return (x + blockSize_ - 1) / blockSize_ * blockSize_;
    }

    // Write a full, block-aligned staging buffer at the current offset.
    Result<void> flushFull() {
        if (auto r = pwriteAll(wbuf_.get(), wfill_, offset_); !r) return r;
        offset_ += static_cast<off_t>(wfill_);
        wfill_ = 0;
        return {};
    }

    // Write the partial staging remainder, zero-padded up to a block so the
    // transfer stays O_DIRECT-legal. Advances the logical offset by the real
    // (unpadded) byte count; the next write must seek() first.
    Result<void> flushTail() {
        if (wfill_ == 0) return {};
        const std::size_t padded = roundUpToBlock(wfill_);
        std::memset(wbuf_.get() + wfill_, 0, padded - wfill_);
        if (auto r = pwriteAll(wbuf_.get(), padded, offset_); !r) return r;
        offset_ += static_cast<off_t>(wfill_);
        wfill_ = 0;
        return {};
    }

    Result<void> pwriteAll(const std::byte* buf, std::size_t len, off_t off) {
        std::size_t done = 0;
        while (done < len) {
            const ssize_t n = ::pwrite(fd_, buf + done, len - done, off + done);
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

    // pread `len` (block-aligned) bytes into an aligned buffer at `off`. Block
    // devices only short-read at EOF, so a partial return ends the fill.
    Result<std::size_t> preadAligned(std::byte* buf, std::size_t len,
                                     off_t off) {
        std::size_t done = 0;
        while (done < len) {
            const ssize_t n = ::pread(fd_, buf + done, len - done, off + done);
            if (n < 0) {
                if (errno == EINTR) continue;
                return ioError(errno, "Read from device failed");
            }
            if (n == 0) break;  // EOF
            done += static_cast<std::size_t>(n);
        }
        return done;
    }

    int fd_ = -1;
    bool direct_ = false;
    unsigned blockSize_ = 512;
    unsigned alignment_ = 4096;
    std::uint64_t devSize_ = 0;
    off_t offset_ = 0;

    AlignedBuf wbuf_;        // write staging (aligned)
    std::size_t wfill_ = 0;  // bytes currently staged in wbuf_

    AlignedBuf rbuf_;        // read bounce / read-ahead (aligned)
    std::size_t rpos_ = 0;   // start of unread data in rbuf_
    std::size_t rlen_ = 0;   // bytes of unread data in rbuf_
};

}  // namespace

std::unique_ptr<IDriveBackend> makeDriveBackend() {
    return std::make_unique<LinuxDriveBackend>();
}

}  // namespace kuiper
