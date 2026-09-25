#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "kuiper/Error.hpp"

namespace kuiper {

// An owning handle to one open raw-device session, handed back by
// IDriveBackend::openForWrite / openForRead. Its lifetime *is* the open device:
// the destructor closes the fd, so a single device is open for exactly as long
// as the handle lives. The backend absorbs alignment and short-I/O internally,
// so callers pass arbitrary lengths; only seek() offsets must be block-aligned.
// See docs: platform-backends.
class IRawDevice {
public:
    virtual ~IRawDevice() = default;  // closes the device

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
};

}  // namespace kuiper
