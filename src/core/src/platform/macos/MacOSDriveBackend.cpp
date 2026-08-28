// macOS drive backend — compilable stub (Phase 4). See docs: platform-backends.

#include "kuiper/platform/IDriveBackend.hpp"

namespace kuiper {
namespace {

class MacOSDriveBackend final : public IDriveBackend {
public:
    Result<DriveList> listDrives() override {
        // noop — real enumeration lands with the macOS port (Phase 4).
        return DriveList{};
    }

    const char* name() const noexcept override { return "macos"; }

    Result<MountedPartition> mount(const Partition&) override {
        return std::unexpected(unsupported().error());
    }

    Result<void> unmountAll(const std::string&) override { return unsupported(); }
    Result<void> openForWrite(const std::string&) override { return unsupported(); }
    Result<void> openForRead(const std::string&) override { return unsupported(); }
    void close() override {}
    Result<void> seek(std::uint64_t) override { return unsupported(); }
    Result<void> write(std::span<const std::byte>) override { return unsupported(); }
    Result<std::size_t> read(std::span<std::byte>) override {
        return std::unexpected(unsupported().error());
    }
    Result<std::uint64_t> deviceSize() override {
        return std::unexpected(unsupported().error());
    }
    Result<void> wipeSignatures() override { return unsupported(); }
    Result<void> flushAndSync() override { return unsupported(); }
    Result<void> rereadPartTable() override { return {}; }

    Result<std::string> parentDisk(const std::string&) override {
        return std::unexpected(unsupported().error());
    }
    Result<std::string> mountpointOf(const std::string&) override {
        return std::unexpected(unsupported().error());
    }

private:
    static Result<void> unsupported() {
        return Err(ErrorCode::UnsupportedPlatform,
                   "Flashing is not yet implemented on macOS");
    }
};

}  // namespace

std::unique_ptr<IDriveBackend> makeDriveBackend() {
    return std::make_unique<MacOSDriveBackend>();
}

}  // namespace kuiper
