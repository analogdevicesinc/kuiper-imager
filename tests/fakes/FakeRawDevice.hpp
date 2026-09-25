#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "kuiper/platform/IRawDevice.hpp"

namespace kuiper::test {

// Shared, inspectable state behind a FakeRawDevice. The backend hands out a
// device that references this; a test keeps its own shared_ptr so it can read the
// backing store and the write log after DriveService has closed the handle.
struct FakeDeviceState {
    std::vector<std::byte> store;
    std::vector<std::pair<std::uint64_t, std::size_t>> writeLog;
    std::optional<std::uint64_t> corruptOffset;
    bool wiped = false;
    bool synced = false;
    bool reReadPartTable = false;
};

// In-memory IRawDevice: a byte vector plus a cursor. Records every write so tests
// can assert the defer-head ordering (partition table committed last), and can
// corrupt one offset to force a verify mismatch. No syscalls — runs anywhere.
class FakeRawDevice final : public IRawDevice {
public:
    explicit FakeRawDevice(std::shared_ptr<FakeDeviceState> state)
        : state_(std::move(state)) {}

    Result<void> seek(std::uint64_t offset) override {
        offset_ = offset;
        return {};
    }

    Result<void> write(std::span<const std::byte> data) override {
        auto& s = *state_;
        if (offset_ + data.size() > s.store.size()) {
            return Err(ErrorCode::DiskFull, "FakeRawDevice: write past end");
        }
        std::memcpy(s.store.data() + offset_, data.data(), data.size());
        s.writeLog.emplace_back(offset_, data.size());
        if (s.corruptOffset && *s.corruptOffset >= offset_ &&
            *s.corruptOffset < offset_ + data.size()) {
            s.store[static_cast<std::size_t>(*s.corruptOffset)] ^= std::byte{0xFF};
        }
        offset_ += data.size();
        return {};
    }

    Result<std::size_t> read(std::span<std::byte> buffer) override {
        auto& s = *state_;
        if (offset_ >= s.store.size()) return std::size_t{0};
        const std::size_t n = std::min<std::size_t>(
            buffer.size(), s.store.size() - static_cast<std::size_t>(offset_));
        std::memcpy(buffer.data(), s.store.data() + offset_, n);
        offset_ += n;
        return n;
    }

    Result<std::uint64_t> deviceSize() override {
        return static_cast<std::uint64_t>(state_->store.size());
    }

    Result<void> wipeSignatures() override {
        state_->wiped = true;
        return {};
    }

    Result<void> flushAndSync() override {
        state_->synced = true;
        return {};
    }

    Result<void> rereadPartTable() override {
        state_->reReadPartTable = true;
        return {};
    }

private:
    std::shared_ptr<FakeDeviceState> state_;
    std::uint64_t offset_ = 0;
};

}  // namespace kuiper::test
