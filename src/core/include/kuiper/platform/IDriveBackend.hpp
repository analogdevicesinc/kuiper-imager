#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "kuiper/Drive.hpp"
#include "kuiper/Error.hpp"
#include "kuiper/MountedPartition.hpp"

namespace kuiper {

class IDriveBackend {
public:
    virtual ~IDriveBackend() = default;

    // Enumerate storage drives, each with its partitions filled in (removable,
    // non-system are the flash candidates). One platform call, whole tree.
    virtual Result<DriveList> listDrives() = 0;

    // Human-readable name of the active platform, e.g. "linux". For diagnostics.
    virtual const char* name() const noexcept = 0;

    // Mount a partition and hand back an RAII handle to its filesystem path. An
    // already-mounted partition is borrowed (left untouched on destruction);
    // otherwise it is mounted to a private temp dir and cleaned up on destruction.
    virtual Result<MountedPartition> mount(const Partition& partition) = 0;

    // --- Raw device I/O (used by DriveService::flash) ------------------------
    //
    // The funnel invariant lives here: only the backend touches syscalls, ioctls,
    // and device nodes. It handles short-write/short-read/EINTR and all O_DIRECT
    // block alignment internally, so callers pass arbitrary lengths. Offsets
    // passed to seek() must be block-aligned. See docs: platform-backends.

    // Unmount every mounted partition of `node` (non-lazy) and swapoff any swap
    // on it. A live holder (md/LVM/LUKS) that cannot be released => DeviceBusy.
    virtual Result<void> unmountAll(const std::string& node) = 0;

    // Open the device for writing (exclusive). Beats the udev auto-remount race
    // with a bounded retry loop. Leaves the fd owned by this object.
    virtual Result<void> openForWrite(const std::string& node) = 0;

    // Open the device read-only (exclusive) for the verify pass.
    virtual Result<void> openForRead(const std::string& node) = 0;

    // Close the currently open fd (if any). Idempotent.
    virtual void close() = 0;

    // Set the current read/write offset. Flushes any pending write-staging and
    // discards read-ahead first. The offset must be block-aligned.
    virtual Result<void> seek(std::uint64_t offset) = 0;

    // Write the whole buffer at the current offset, advancing it. Bytes may be
    // staged in an aligned buffer and not reach the media until the staging
    // buffer fills or flushAndSync() runs. Loops over short writes / EINTR.
    virtual Result<void> write(std::span<const std::byte> data) = 0;

    // Read into the buffer at the current offset, advancing it. Returns the byte
    // count actually read (0 == EOF); fills the buffer unless EOF is hit.
    // Accepts arbitrary lengths; the backend reads block-aligned internally.
    virtual Result<std::size_t> read(std::span<std::byte> buffer) = 0;

    // Capacity of the open device in bytes (source of truth for the size guard).
    virtual Result<std::uint64_t> deviceSize() = 0;

    // Zero the first 4 MiB and last 1 MiB so a smaller image can't leave a ghost
    // partition table / backup GPT, and a partial write never looks bootable.
    virtual Result<void> wipeSignatures() = 0;

    // Flush any staged write tail (zero-padded to a block), then fsync and drop
    // the block device's page cache so the verify pass reads media, not cache.
    virtual Result<void> flushAndSync() = 0;

    // Ask the kernel to re-read the partition table (best-effort; never fatal).
    virtual Result<void> rereadPartTable() = 0;

    // --- Topology queries (used by DriveService for the preloader write) ------

    // Whole disk that owns a partition node, e.g. "/dev/mmcblk0p1" =>
    // "/dev/mmcblk0". Empty result if `node` is already a whole disk.
    virtual Result<std::string> parentDisk(const std::string& node) = 0;

    // Current mountpoint of a node, or empty if it is not mounted.
    virtual Result<std::string> mountpointOf(const std::string& node) = 0;
};

// Factory: returns the IDriveBackend for the host platform. Defined per-OS so
// only the current platform's implementation is compiled/linked.
std::unique_ptr<IDriveBackend> makeDriveBackend();

}  // namespace kuiper
