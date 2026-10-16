#include "platform/common/AlignedRawDevice.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace kuiper::detail {
namespace {

constexpr std::uint64_t kMiB = 1u << 20;
constexpr std::size_t kAlignBuf = 4u << 20;    // aligned staging / bounce (4 MiB)
constexpr std::uint64_t kWipeHead = 4 * kMiB;  // clobber MBR/GPT + first parts
constexpr std::uint64_t kWipeTail = 1 * kMiB;  // clobber backup GPT

}  // namespace

AlignedRawDevice::AlignedRawDevice(std::unique_ptr<BlockIo> io)
    : io_(std::move(io)) {}

Result<void> AlignedRawDevice::init() {
    blockSize_ = std::max<unsigned>(io_->blockSize(), 1u);
    alignment_ = std::max<unsigned>(blockSize_, 4096u);
    devSize_ = io_->deviceSize();

    wbuf_ = AlignedBuf(kAlignBuf, alignment_);
    rbuf_ = AlignedBuf(kAlignBuf, alignment_);
    if (!wbuf_ || !rbuf_) {
        return Err(ErrorCode::Unknown, "Out of memory (aligned I/O buffers)");
    }
    // Drop any pages cached before we opened so a verify read hits the media.
    // Harmless (a no-op cost) when the open mode already bypasses the cache.
    io_->dropCache();
    return {};
}

Result<void> AlignedRawDevice::seek(std::uint64_t offset) {
    if (auto r = flushTail(); !r) return r;  // commit any staged write bytes
    rpos_ = 0;                               // discard read-ahead
    rlen_ = 0;
    offset_ = offset;
    return {};
}

Result<void> AlignedRawDevice::write(std::span<const std::byte> data) {
    // A fresh staging run must start block-aligned. flushTail() advances offset_ by
    // the unpadded byte count, so it can leave offset_ unaligned; the caller must
    // seek() (which realigns) before writing again after a tail flush /
    // flushAndSync(). Direct/unbuffered pwrite rejects an unaligned offset, so pin
    // the invariant here rather than fail only on real hardware.
    assert(wfill_ != 0 || offset_ % blockSize_ == 0);
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

Result<std::size_t> AlignedRawDevice::read(std::span<std::byte> buffer) {
    std::byte* out = buffer.data();
    const std::size_t want = buffer.size();
    std::size_t total = 0;
    while (total < want) {
        if (rlen_ == 0) {
            if (devSize_ != 0 && offset_ >= devSize_) {
                break;  // EOF
            }
            const std::uint64_t avail =
                devSize_ ? devSize_ - offset_ : kAlignBuf;
            const std::size_t toRead = static_cast<std::size_t>(
                std::min<std::uint64_t>(kAlignBuf, avail));
            auto n = io_->pread(rbuf_.get(), toRead, offset_);
            if (!n) return std::unexpected(n.error());
            if (*n == 0) break;  // EOF
            offset_ += *n;
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

Result<std::uint64_t> AlignedRawDevice::deviceSize() {
    if (devSize_ != 0) return devSize_;
    devSize_ = io_->deviceSize();
    if (devSize_ == 0) {
        return Err(ErrorCode::Unknown, "Cannot query device size");
    }
    return devSize_;
}

Result<void> AlignedRawDevice::wipeSignatures() {
    if (devSize_ == 0) {
        if (auto s = deviceSize(); !s) return std::unexpected(s.error());
    }

    AlignedBuf zeros(static_cast<std::size_t>(kMiB), alignment_);
    if (!zeros) {
        return Err(ErrorCode::Unknown, "Out of memory (aligned wipe buffer)");
    }
    std::memset(zeros.get(), 0, static_cast<std::size_t>(kMiB));

    // Ranges are block-aligned (0, kMiB multiples, and devSize_ which the kernel
    // reports as a multiple of the logical block size), so each write stays
    // alignment-legal for direct/unbuffered I/O.
    auto zeroRange = [&](std::uint64_t begin,
                         std::uint64_t end) -> Result<void> {
        std::uint64_t off = begin;
        while (off < end) {
            const std::size_t chunk =
                static_cast<std::size_t>(std::min<std::uint64_t>(end - off, kMiB));
            if (auto r = io_->pwrite(zeros.get(), chunk, off); !r) return r;
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
    if (auto r = io_->sync(); !r) return r;  // make sure the wipe reaches media
    return {};
}

Result<void> AlignedRawDevice::flushAndSync() {
    if (auto r = flushTail(); !r) return r;  // commit the staged tail first
    if (auto r = io_->sync(); !r) return r;
    io_->dropCache();  // force the verify pass to re-read from media
    return {};
}

Result<void> AlignedRawDevice::rereadPartTable() {
    io_->rereadPartTable();  // best effort; harmless if it fails
    return {};
}

std::size_t AlignedRawDevice::roundUpToBlock(std::size_t x) const {
    return (x + blockSize_ - 1) / blockSize_ * blockSize_;
}

Result<void> AlignedRawDevice::flushFull() {
    if (auto r = io_->pwrite(wbuf_.get(), wfill_, offset_); !r) return r;
    offset_ += wfill_;
    wfill_ = 0;
    return {};
}

Result<void> AlignedRawDevice::flushTail() {
    if (wfill_ == 0) return {};
    const std::size_t padded = roundUpToBlock(wfill_);
    std::memset(wbuf_.get() + wfill_, 0, padded - wfill_);
    if (auto r = io_->pwrite(wbuf_.get(), padded, offset_); !r) return r;
    offset_ += wfill_;  // advance by the real (unpadded) byte count
    wfill_ = 0;
    return {};
}

}  // namespace kuiper::detail
