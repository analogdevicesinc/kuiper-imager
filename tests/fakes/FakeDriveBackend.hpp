#pragma once

#include <memory>
#include <string>
#include <utility>

#include "kuiper/platform/IDriveBackend.hpp"

#include "fakes/FakeRawDevice.hpp"

namespace kuiper::test {

// Scriptable IDriveBackend: returns a fixed drive list and capabilities, and
// hands out a FakeRawDevice backed by shared state the test can inspect. Set the
// knobs before moving it into a DriveService.
class FakeDriveBackend final : public IDriveBackend {
public:
    FakeDriveBackend() = default;

    const char* name() const noexcept override { return "fake"; }
    DriveCapabilities capabilities() const noexcept override { return caps_; }
    Result<DriveList> listDrives() override { return drives_; }

    Result<MountedPartition> mount(const Partition&) override {
        return Err(ErrorCode::UnsupportedPlatform, "FakeDriveBackend: no mount");
    }

    Result<void> unmountAll(const std::string&) override {
        ++unmountCalls_;
        return {};
    }

    Result<std::unique_ptr<IRawDevice>> openForWrite(
        const std::string&) override {
        return openAny();
    }
    Result<std::unique_ptr<IRawDevice>> openForRead(
        const std::string&) override {
        return openAny();
    }

    void setDrives(DriveList drives) { drives_ = std::move(drives); }
    void setCapabilities(DriveCapabilities caps) { caps_ = caps; }
    void setDevice(std::shared_ptr<FakeDeviceState> device) {
        device_ = std::move(device);
    }

    int unmountCalls() const noexcept { return unmountCalls_; }

private:
    Result<std::unique_ptr<IRawDevice>> openAny() {
        if (!device_) {
            return Err(ErrorCode::DeviceRemoved, "FakeDriveBackend: no device");
        }
        std::unique_ptr<IRawDevice> dev =
            std::make_unique<FakeRawDevice>(device_);
        return dev;
    }

    DriveList drives_;
    DriveCapabilities caps_{true, true, true};
    std::shared_ptr<FakeDeviceState> device_;
    int unmountCalls_ = 0;
};

}  // namespace kuiper::test
