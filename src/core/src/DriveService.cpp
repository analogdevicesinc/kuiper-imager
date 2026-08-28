#include "kuiper/DriveService.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "util/Sha256.hpp"

namespace kuiper {
namespace {

namespace fs = std::filesystem;

using Clock = std::chrono::steady_clock;

using detail::Sha256;

double elapsed(Clock::time_point t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
}

constexpr std::size_t kOpenBlock = 1u << 20;   // libarchive read block (1 MiB)
constexpr std::size_t kChunk = 4u << 20;       // decompress / verify buffer (4 MiB)
constexpr std::int64_t kAmbiguousZip = 1 << 20;  // >1 entry above this => ambiguous

// Written LAST, after the body verifies, so an interrupted flash never leaves a
// bootable-looking card. 1 MiB covers the partition table and is block-aligned on
// every real device. See docs: flash-pipeline (defer-head).
constexpr std::uint64_t kDeferHead = 1u << 20;

// RAII wrapper for a libarchive read handle (free also closes).
struct Reader {
    archive* a = archive_read_new();
    ~Reader() {
        if (a) archive_read_free(a);
    }
    operator archive*() const { return a; }
};

// support_format_all is avoided deliberately: tar/ISO can mis-bid on a raw disk
// image. A missing codec surfaces here rather than mid-stream.
Result<void> configure(archive* a) {
    auto enable = [&](int rc, const char* what) -> Result<void> {
        if (rc == ARCHIVE_FATAL) {
            const char* e = archive_error_string(a);
            return Err(ErrorCode::UnsupportedPlatform,
                       std::string("Cannot enable decompressor: ") + what,
                       e ? e : "");
        }
        return {};
    };
    if (auto r = enable(archive_read_support_filter_gzip(a), "gzip"); !r) return r;
    if (auto r = enable(archive_read_support_filter_xz(a), "xz"); !r) return r;
    if (auto r = enable(archive_read_support_filter_zstd(a), "zstd"); !r) return r;
    if (auto r = enable(archive_read_support_filter_bzip2(a), "bzip2"); !r) return r;
    archive_read_support_format_raw(a);
    archive_read_support_format_zip(a);
    return {};
}

bool looksLikeZip(const fs::path& path) {
    std::error_code ec;
    std::uintmax_t sz = fs::file_size(path, ec);
    if (ec || sz < 4) return false;
    FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return false;
    unsigned char magic[4] = {0};
    std::size_t n = std::fread(magic, 1, 4, f);
    std::fclose(f);
    return n == 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 &&
           magic[3] == 0x04;
}

Result<std::string> chooseZipEntry(const fs::path& path) {
    Reader ar;
    if (!ar.a) return Err(ErrorCode::Unknown, "Out of memory (libarchive)");
    if (auto r = configure(ar); !r) return std::unexpected(r.error());
    if (archive_read_open_filename(ar, path.string().c_str(), kOpenBlock) !=
        ARCHIVE_OK) {
        const char* e = archive_error_string(ar);
        return Err(ErrorCode::InvalidImage, "Cannot open archive", e ? e : "");
    }

    struct Cand {
        std::string name;
        std::int64_t size;
    };
    std::vector<Cand> regs;
    archive_entry* entry = nullptr;
    int rc;
    while ((rc = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) == AE_IFREG) {
            std::int64_t sz = archive_entry_size_is_set(entry)
                                  ? archive_entry_size(entry)
                                  : -1;
            const char* nm = archive_entry_pathname(entry);
            regs.push_back({nm ? nm : "", sz});
        }
        archive_read_data_skip(ar);
    }
    if (rc != ARCHIVE_EOF) {
        const char* e = archive_error_string(ar);
        return Err(ErrorCode::InvalidImage, "Corrupt zip archive", e ? e : "");
    }
    if (regs.empty()) {
        return Err(ErrorCode::InvalidImage, "Zip contains no regular files");
    }
    std::sort(regs.begin(), regs.end(),
              [](const Cand& x, const Cand& y) { return x.size > y.size; });
    const int big = static_cast<int>(std::count_if(
        regs.begin(), regs.end(),
        [](const Cand& c) { return c.size >= kAmbiguousZip; }));
    if (big > 1) {
        return Err(ErrorCode::InvalidImage,
                   "Zip has multiple large files; cannot pick the image",
                   "Extract the intended .img and flash it directly");
    }
    return regs.front().name;
}

// Streaming write state shared with the decompressor. See docs: flash-pipeline.
struct WriteCtx {
    IDriveBackend& dev;
    std::uint64_t capacity;
    std::uint64_t deferHead;
    std::vector<std::byte>& head;
    Sha256& fullHash;               // whole image, in order; the reported result
    Sha256* bodyHash;               // body only; null when not verifying
    std::uint64_t compressedTotal;
    const ProgressFn& onProgress;
    const CancelToken& cancel;
    std::uint64_t written = 0;      // total image bytes consumed (head + body)
    std::uint64_t bodyWritten = 0;  // bytes handed to dev.write() (body only)
    double lastFraction = 0.0;
    double decompressSec = 0.0;     // diagnostic
    double deviceWriteSec = 0.0;    // diagnostic
};

// The device offset must already be positioned at `deferHead` before the body is
// streamed (the head region is captured, not written here).
Result<void> streamEntry(archive* a, WriteCtx& ctx) {
    std::vector<std::byte> buf(kChunk);
    for (;;) {
        if (ctx.cancel.isCancelled()) {
            return Err(ErrorCode::UserCancelled, "Flash cancelled");
        }
        const auto tDec = Clock::now();
        la_ssize_t n = archive_read_data(a, buf.data(), buf.size());
        ctx.decompressSec += elapsed(tDec);
        if (n == 0) break;
        if (n < 0) {
            if (n == ARCHIVE_WARN) continue;  // recoverable; retry read
            const char* e = archive_error_string(a);
            return Err(ErrorCode::InvalidImage, "Decompression failed",
                       e ? e : "");
        }
        const auto count = static_cast<std::size_t>(n);
        if (ctx.written + count > ctx.capacity) {
            return Err(ErrorCode::DiskFull,
                       "Image is larger than the target device");
        }

        const std::byte* p = buf.data();
        std::size_t left = count;

        // Capture the deferred head (partition table); do not write it yet.
        if (ctx.written < ctx.deferHead) {
            const std::size_t take = static_cast<std::size_t>(
                std::min<std::uint64_t>(left, ctx.deferHead - ctx.written));
            ctx.head.insert(ctx.head.end(), p, p + take);
            ctx.fullHash.update(p, take);
            ctx.written += take;
            p += take;
            left -= take;
        }

        if (left > 0) {
            const auto tWr = Clock::now();
            auto wr = ctx.dev.write(std::span<const std::byte>(p, left));
            ctx.deviceWriteSec += elapsed(tWr);
            if (!wr) return wr;
            ctx.fullHash.update(p, left);
            if (ctx.bodyHash) ctx.bodyHash->update(p, left);
            ctx.written += left;
            ctx.bodyWritten += left;
        }

        if (ctx.onProgress) {
            double frac = 0.0;
            if (ctx.compressedTotal > 0) {
                const auto consumed =
                    static_cast<std::uint64_t>(archive_filter_bytes(a, -1));
                frac = static_cast<double>(consumed) /
                       static_cast<double>(ctx.compressedTotal);
            }
            frac = std::clamp(frac, 0.0, 1.0);
            if (frac < ctx.lastFraction) frac = ctx.lastFraction;  // monotonic
            ctx.lastFraction = frac;
            ctx.onProgress(Progress{Progress::Phase::Writing, ctx.written, 0,
                                    frac, "Writing image"});
        }
    }
    return {};
}

Result<void> decompressToDevice(const fs::path& imagePath, WriteCtx& ctx) {
    if (looksLikeZip(imagePath)) {
        auto target = chooseZipEntry(imagePath);
        if (!target) return std::unexpected(target.error());

        Reader ar;
        if (!ar.a) return Err(ErrorCode::Unknown, "Out of memory (libarchive)");
        if (auto r = configure(ar); !r) return std::unexpected(r.error());
        if (archive_read_open_filename(ar, imagePath.string().c_str(),
                                       kOpenBlock) != ARCHIVE_OK) {
            const char* e = archive_error_string(ar);
            return Err(ErrorCode::InvalidImage, "Cannot open archive",
                       e ? e : "");
        }
        archive_entry* entry = nullptr;
        int rc;
        while ((rc = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
            const char* nm = archive_entry_pathname(entry);
            if (nm && *target == nm) {
                return streamEntry(ar, ctx);
            }
            archive_read_data_skip(ar);
        }
        return Err(ErrorCode::InvalidImage, "Selected zip entry disappeared");
    }

    // Raw / single-stream (.img, .img.gz, .img.xz, .img.zst, .img.bz2).
    Reader ar;
    if (!ar.a) return Err(ErrorCode::Unknown, "Out of memory (libarchive)");
    if (auto r = configure(ar); !r) return std::unexpected(r.error());
    if (archive_read_open_filename(ar, imagePath.string().c_str(), kOpenBlock) !=
        ARCHIVE_OK) {
        const char* e = archive_error_string(ar);
        return Err(ErrorCode::InvalidImage, "Cannot open image", e ? e : "");
    }
    archive_entry* entry = nullptr;
    int rc = archive_read_next_header(ar, &entry);
    if (rc != ARCHIVE_OK) {
        const char* e = archive_error_string(ar);
        return Err(ErrorCode::InvalidImage, "Unrecognized image format",
                   e ? e : "");
    }
    return streamEntry(ar, ctx);
}

// The backend handles all block alignment; callers pass plain byte counts.
Result<void> hashDeviceRange(IDriveBackend& dev, std::uint64_t length,
                             Sha256& hash,
                             const ProgressFn& onProgress,
                             const CancelToken& cancel, const char* message) {
    std::vector<std::byte> buf(kChunk);
    std::uint64_t remaining = length;
    while (remaining > 0) {
        if (cancel.isCancelled()) {
            return Err(ErrorCode::UserCancelled, "Verification cancelled");
        }
        const std::size_t want = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buf.size()));
        auto n = dev.read(std::span<std::byte>(buf.data(), want));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) {
            return Err(ErrorCode::DeviceRemoved,
                       "Unexpected end of device during verify");
        }
        hash.update(buf.data(), *n);
        remaining -= *n;
        if (onProgress) {
            const double frac =
                length ? static_cast<double>(length - remaining) /
                             static_cast<double>(length)
                       : 1.0;
            onProgress(Progress{Progress::Phase::Verifying, length - remaining,
                                length, frac, message});
        }
    }
    return {};
}

Result<std::uint64_t> statSource(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return Err(ErrorCode::InvalidImage,
                   "Not a readable file: " + path.string());
    }
    const std::uint64_t size = fs::file_size(path, ec);
    if (ec || size == 0) {
        return Err(ErrorCode::InvalidImage, "File is empty: " + path.string());
    }
    return size;
}

// Runs IDriveBackend::close() on every exit path; set dev=nullptr to disarm.
struct Closer {
    IDriveBackend* dev = nullptr;
    ~Closer() {
        if (dev) dev->close();
    }
};

}  // namespace

DriveService::DriveService() : backend_(makeDriveBackend()) {}

DriveService::DriveService(std::unique_ptr<IDriveBackend> backend)
    : backend_(std::move(backend)) {}

Result<DriveList> DriveService::listDrives() {
    if (!backend_) {
        return Err(ErrorCode::UnsupportedPlatform,
                   "No drive backend available for this platform");
    }
    return backend_->listDrives();
}

Result<MountedPartition> DriveService::mount(const Partition& partition) {
    if (!backend_) {
        return Err(ErrorCode::UnsupportedPlatform,
                   "No drive backend available for this platform");
    }
    return backend_->mount(partition);
}

Result<FlashSummary> DriveService::flash(const std::string& driveNode,
                                         const std::string& imagePath,
                                         const WriteOptions& options,
                                         ProgressFn onProgress,
                                         CancelToken cancel) {
    if (!backend_) {
        return Err(ErrorCode::UnsupportedPlatform,
                   "No drive backend available for this platform");
    }
    auto report = [&](Progress::Phase phase, const char* msg) {
        if (onProgress) onProgress(Progress{phase, 0, 0, 0.0, msg});
    };

    FlashTimings timings;
    const auto tStart = Clock::now();

    // Re-enumerate; never trust cached metadata. --force bypasses the presence and
    // removable/system checks (loop devices, power users); the backend still
    // refuses anything that isn't a block device.
    if (!options.force) {
        auto list = backend_->listDrives();
        if (!list) return std::unexpected(list.error());
        const Drive* found = nullptr;
        for (const auto& d : *list) {
            if (d.node == driveNode) {
                found = &d;
                break;
            }
        }
        if (!found) {
            return Err(ErrorCode::DeviceRemoved,
                       "Drive not found: " + driveNode,
                       "Run 'kli list-drives'; use --force for loop devices");
        }
        if (!found->isRemovable || found->isSystem) {
            return Err(ErrorCode::PermissionDenied,
                       "Refusing to flash a non-removable/system drive: " +
                           driveNode,
                       "Pass --force only if you are certain");
        }
    }

    fs::path img(imagePath);
    auto srcSize = statSource(img);
    if (!srcSize) return std::unexpected(srcSize.error());
    const std::uint64_t compressedSize = *srcSize;

    report(Progress::Phase::Preparing, "Preparing device");

    if (auto r = backend_->unmountAll(driveNode); !r) {
        return std::unexpected(r.error());
    }

    // Bounded EBUSY retry lives inside the backend (udev may re-open the device).
    if (auto r = backend_->openForWrite(driveNode); !r) {
        return std::unexpected(r.error());
    }
    Closer closer{backend_.get()};

    auto capacity = backend_->deviceSize();
    if (!capacity) return std::unexpected(capacity.error());

    // Wipe signatures so a smaller image can't leave a ghost partition table.
    if (auto r = backend_->wipeSignatures(); !r) {
        return std::unexpected(r.error());
    }

    // Decompress and write the body, deferring the head to offset kDeferHead.
    timings.prepareSec = elapsed(tStart);
    report(Progress::Phase::Writing, "Writing image");
    Sha256 fullHash;
    Sha256 bodyHash;
    std::vector<std::byte> head;
    head.reserve(static_cast<std::size_t>(kDeferHead));

    if (auto r = backend_->seek(kDeferHead); !r) {
        return std::unexpected(r.error());
    }
    WriteCtx ctx{*backend_, *capacity, kDeferHead, head,
                 fullHash,  options.verify ? &bodyHash : nullptr,
                 compressedSize, onProgress, cancel};
    const auto tWrite = Clock::now();
    if (auto r = decompressToDevice(img, ctx); !r) {
        // The head was never written, so the card is non-bootable by
        // construction — no re-wipe needed on this path.
        return std::unexpected(r.error());
    }
    timings.writeSec = elapsed(tWrite);
    timings.decompressSec = ctx.decompressSec;
    timings.deviceWriteSec = ctx.deviceWriteSec;
    const std::uint64_t written = ctx.written;
    const std::uint64_t bodyWritten = ctx.bodyWritten;

    report(Progress::Phase::Finalizing, "Flushing to device");
    const auto tFlush = Clock::now();
    if (auto r = backend_->flushAndSync(); !r) {
        return std::unexpected(r.error());
    }
    timings.flushSec = elapsed(tFlush);

    const std::string fullDigest = fullHash.hex();

    // Verify the body BEFORE committing the head: on failure the head is still
    // unwritten, so the card stays non-bootable. --no-verify skips this; the head
    // is still written last and checked below, so a failure never looks bootable.
    const auto tVerify = Clock::now();
    if (options.verify && bodyWritten > 0) {
        report(Progress::Phase::Verifying, "Verifying image");
        if (auto r = backend_->seek(kDeferHead); !r) {
            return std::unexpected(r.error());
        }
        Sha256 readBody;
        if (auto r = hashDeviceRange(*backend_, bodyWritten, readBody, onProgress,
                                     cancel, "Verifying image");
            !r) {
            return std::unexpected(r.error());
        }
        if (readBody.hex() != bodyHash.hex()) {
            return Err(ErrorCode::HashMismatch,
                       "Verification failed: device body differs from image");
        }
    }

    timings.verifySec = elapsed(tVerify);

    // Commit the head (partition table) last, then flush and re-read.
    const auto tHead = Clock::now();
    report(Progress::Phase::Finalizing, "Writing partition table");
    if (auto r = backend_->seek(0); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = backend_->write(std::span<const std::byte>(head.data(),
                                                           head.size()));
        !r) {
        return std::unexpected(r.error());
    }
    if (auto r = backend_->flushAndSync(); !r) {
        return std::unexpected(r.error());
    }
    backend_->rereadPartTable();  // best-effort; ignore result

    // Verify the committed head matches the bytes we retained.
    report(Progress::Phase::Verifying, "Verifying partition table");
    if (auto r = backend_->seek(0); !r) {
        return std::unexpected(r.error());
    }
    std::vector<std::byte> readHead(head.size());
    std::size_t got = 0;
    while (got < readHead.size()) {
        auto n = backend_->read(
            std::span<std::byte>(readHead.data() + got, readHead.size() - got));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) {
            return Err(ErrorCode::DeviceRemoved,
                       "Unexpected end of device during verify");
        }
        got += *n;
    }
    if (readHead != head) {
        backend_->wipeSignatures();  // best effort: don't leave a bad table live
        return Err(ErrorCode::HashMismatch,
                   "Verification failed: partition table differs from image");
    }

    timings.headSec = elapsed(tHead);

    backend_->close();
    closer.dev = nullptr;  // already closed

    timings.totalSec = elapsed(tStart);
    report(Progress::Phase::Done, "Done");
    return FlashSummary{written, fullDigest, timings};
}

Result<FlashSummary> DriveService::writePreloader(
    const std::string& partitionDevice, const std::string& preloaderFile,
    const WriteOptions& options, ProgressFn onProgress, CancelToken cancel) {
    if (!backend_) {
        return Err(ErrorCode::UnsupportedPlatform,
                   "No drive backend available for this platform");
    }
    auto report = [&](Progress::Phase phase, const char* msg) {
        if (onProgress) onProgress(Progress{phase, 0, 0, 0.0, msg});
    };

    auto srcSize = statSource(preloaderFile);
    if (!srcSize) return std::unexpected(srcSize.error());
    const std::uint64_t preloaderSize = *srcSize;

    // A partition is a plausible live filesystem, so refuse a mounted one; this
    // also closes the openForWrite -> EBUSY -> unmountAll -> overwrite hazard.
    if (!options.force) {
        auto mnt = backend_->mountpointOf(partitionDevice);
        if (!mnt) return std::unexpected(mnt.error());
        if (!mnt->empty()) {
            return Err(ErrorCode::PermissionDenied,
                       "Target partition is mounted at " + *mnt +
                           "; refusing to overwrite: " + partitionDevice,
                       "Unmount it first, or pass --force if you are certain.");
        }

        // Keyed off the owning disk: partitions don't appear in listDrives.
        auto disk = backend_->parentDisk(partitionDevice);
        if (!disk) return std::unexpected(disk.error());
        const std::string owner = disk->empty() ? partitionDevice : *disk;
        auto list = backend_->listDrives();
        if (!list) return std::unexpected(list.error());
        const Drive* found = nullptr;
        for (const auto& dv : *list) {
            if (dv.node == owner) {
                found = &dv;
                break;
            }
        }
        if (!found || !found->isRemovable || found->isSystem) {
            return Err(ErrorCode::PermissionDenied,
                       "Refusing to write to a non-removable/system disk: " +
                           owner,
                       "Pass --force only if you are certain");
        }
    }

    report(Progress::Phase::Preparing, "Preparing partition");

    if (auto r = backend_->unmountAll(partitionDevice); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = backend_->openForWrite(partitionDevice); !r) {
        return std::unexpected(r.error());
    }
    Closer closer{backend_.get()};

    auto capacity = backend_->deviceSize();
    if (!capacity) return std::unexpected(capacity.error());
    if (preloaderSize > *capacity) {
        return Err(ErrorCode::DiskFull,
                   "Preloader is larger than the target partition: " +
                       partitionDevice);
    }

    // A preloader must start at the partition head — no deferral.
    report(Progress::Phase::Writing, "Writing preloader");
    if (auto r = backend_->seek(0); !r) return std::unexpected(r.error());

    std::ifstream in(preloaderFile, std::ios::binary);
    if (!in) {
        return Err(ErrorCode::InvalidImage,
                   "Not a readable file: " + preloaderFile);
    }
    Sha256 hash;
    std::vector<char> buf(kChunk);
    std::uint64_t written = 0;
    while (written < preloaderSize) {
        if (cancel.isCancelled()) {
            return Err(ErrorCode::UserCancelled, "Preloader write cancelled");
        }
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) break;
        const auto n = static_cast<std::size_t>(got);
        const auto* bytes = reinterpret_cast<const std::byte*>(buf.data());
        if (auto r = backend_->write(std::span<const std::byte>(bytes, n)); !r) {
            return std::unexpected(r.error());
        }
        hash.update(bytes, n);
        written += n;
        if (onProgress) {
            Progress p;
            p.phase = Progress::Phase::Writing;
            p.bytesDone = written;
            p.bytesTotal = preloaderSize;
            p.fraction = preloaderSize
                             ? static_cast<double>(written) / preloaderSize
                             : 0.0;
            p.message = "Writing preloader";
            onProgress(p);
        }
    }

    report(Progress::Phase::Finalizing, "Flushing to device");
    if (auto r = backend_->flushAndSync(); !r) {
        return std::unexpected(r.error());
    }

    const std::string digest = hash.hex();

    if (options.verify) {
        report(Progress::Phase::Verifying, "Verifying preloader");
        if (auto r = backend_->seek(0); !r) return std::unexpected(r.error());
        Sha256 readBack;
        if (auto r = hashDeviceRange(*backend_, written, readBack, onProgress,
                                     cancel, "Verifying preloader");
            !r) {
            return std::unexpected(r.error());
        }
        if (readBack.hex() != digest) {
            return Err(ErrorCode::HashMismatch,
                       "Verification failed: partition differs from preloader");
        }
    }

    backend_->close();
    closer.dev = nullptr;  // already closed

    report(Progress::Phase::Done, "Done");
    return FlashSummary{written, digest};
}

const char* DriveService::backendName() const noexcept {
    return backend_ ? backend_->name() : "none";
}

}  // namespace kuiper
