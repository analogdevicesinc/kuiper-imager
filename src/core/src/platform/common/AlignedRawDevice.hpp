#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "kuiper/Error.hpp"
#include "kuiper/platform/IRawDevice.hpp"
#include "platform/common/AlignedBuf.hpp"
#include "platform/common/BlockIo.hpp"

namespace kuiper::detail {

// The portable raw-device engine: one IRawDevice implementation shared by every
// OS backend. It owns the aligned write-staging and read-bounce buffers and
// absorbs all alignment, short-I/O, and cache concerns, so the per-OS BlockIo
// only has to do positioned, aligned reads/writes on its native handle. The
// backend constructs a BlockIo for an open device, wraps it here, and hands the
// result back from openForWrite/openForRead. See docs: platform-backends.
class AlignedRawDevice final : public IRawDevice {
public:
    explicit AlignedRawDevice(std::unique_ptr<BlockIo> io);

    // Allocate the aligned staging/bounce buffers from the device geometry and
    // drop any stale cached pages. Call once, right after construction.
    Result<void> init();

    Result<void> seek(std::uint64_t offset) override;
    Result<void> write(std::span<const std::byte> data) override;
    Result<std::size_t> read(std::span<std::byte> buffer) override;
    Result<std::uint64_t> deviceSize() override;
    Result<void> wipeSignatures() override;
    Result<void> flushAndSync() override;
    Result<void> rereadPartTable() override;

private:
    std::size_t roundUpToBlock(std::size_t x) const;
    Result<void> flushFull();  // write a full staging buffer
    Result<void> flushTail();  // write the partial remainder, zero-padded

    std::unique_ptr<BlockIo> io_;
    unsigned blockSize_ = 512;
    unsigned alignment_ = 4096;
    std::uint64_t devSize_ = 0;
    std::uint64_t offset_ = 0;

    AlignedBuf wbuf_;        // write staging (aligned)
    std::size_t wfill_ = 0;  // bytes currently staged in wbuf_

    AlignedBuf rbuf_;        // read bounce / read-ahead (aligned)
    std::size_t rpos_ = 0;   // start of unread data in rbuf_
    std::size_t rlen_ = 0;   // bytes of unread data in rbuf_
};

}  // namespace kuiper::detail
