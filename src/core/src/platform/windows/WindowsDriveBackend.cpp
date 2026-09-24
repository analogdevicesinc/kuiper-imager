// Windows drive backend — compilable stub (Phase 4). See docs: platform-backends.

#include "kuiper/platform/IDriveBackend.hpp"

namespace kuiper {
namespace {

class WindowsDriveBackend final : public IDriveBackend {
public:
    const char* name() const noexcept override { return "windows"; }

    Result<DriveList> listDrives() override {
        // noop — real enumeration lands with the Windows port (Phase 4).
        return DriveList{};
    }

    Result<MountedPartition> mount(const Partition&) override {
        return std::unexpected(unsupported().error());
    }

    Result<void> unmountAll(const std::string&) override { return unsupported(); }

    Result<std::unique_ptr<IRawDevice>> openForWrite(const std::string&) override {
        return std::unexpected(unsupported().error());
    }
    Result<std::unique_ptr<IRawDevice>> openForRead(const std::string&) override {
        return std::unexpected(unsupported().error());
    }

private:
    static Result<void> unsupported() {
        return Err(ErrorCode::UnsupportedPlatform,
                   "Flashing is not yet implemented on Windows");
    }
};

}  // namespace

std::unique_ptr<IDriveBackend> makeDriveBackend() {
    return std::make_unique<WindowsDriveBackend>();
}

}  // namespace kuiper
