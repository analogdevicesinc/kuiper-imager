#pragma once

#include <cstddef>
#include <cstdint>

#include "kuiper/Error.hpp"

namespace kuiper::detail {

// The thin, OS-specific primitive behind AlignedRawDevice. It carries only
// positioned, already-aligned operations on an open device session — no staging,
// no bounce buffers, no alignment bookkeeping (AlignedRawDevice owns all of
// that). Each OS backend implements this over its native handle (a POSIX fd, a
// Win32 HANDLE); the destructor closes the handle. Geometry (blockSize,
// deviceSize) is queried once at open time and returned from cache here.
//
// Contract for implementers:
//  - pread/pwrite receive block-aligned `off` and `len` and a block-aligned
//    buffer; they must loop over short I/O and EINTR, and pread stops at EOF
//    (returning the short count) rather than erroring.
//  - sync() makes written data durable on the media (and may drop the OS block
//    cache); dropCache() hints the OS to forget cached pages so a verify read
//    hits the media. Both may be near-no-ops where the open mode already bypasses
//    the cache (O_DIRECT / FILE_FLAG_NO_BUFFERING).
struct BlockIo {
    virtual ~BlockIo() = default;

    // Logical block (sector) size and total device capacity in bytes, cached at
    // open time. deviceSize() returns 0 if the capacity could not be determined.
    virtual unsigned blockSize() const noexcept = 0;
    virtual std::uint64_t deviceSize() const noexcept = 0;

    // Positioned read/write at `off` (block-aligned). pwrite writes the whole
    // buffer or errors; pread returns the number of bytes read (0 == EOF).
    virtual Result<std::size_t> pread(std::byte* buf, std::size_t len,
                                      std::uint64_t off) = 0;
    virtual Result<void> pwrite(const std::byte* buf, std::size_t len,
                                std::uint64_t off) = 0;

    // Make written data durable (fsync / FlushFileBuffers, plus a cache flush
    // where the platform offers one).
    virtual Result<void> sync() = 0;

    // Best-effort: drop cached pages so a subsequent read hits the media.
    virtual void dropCache() = 0;

    // Best-effort: ask the OS to re-read the partition table.
    virtual void rereadPartTable() = 0;
};

}  // namespace kuiper::detail
