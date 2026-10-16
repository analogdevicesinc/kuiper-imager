#pragma once

#include <memory>
#include <string>

#include "kuiper/Drive.hpp"
#include "kuiper/Error.hpp"
#include "kuiper/MountedPartition.hpp"
#include "kuiper/platform/DriveCapabilities.hpp"
#include "kuiper/platform/IRawDevice.hpp"

namespace kuiper {

class IDriveBackend {
public:
    virtual ~IDriveBackend() = default;

    // Human-readable name of the active platform, e.g. "linux". For diagnostics.
    virtual const char* name() const noexcept = 0;

    // What this backend can do on the host platform (used to gate commands up
    // front). The Linux backend reports everything; the stubs report nothing.
    virtual DriveCapabilities capabilities() const noexcept = 0;

    // Enumerate storage drives, each with its partitions filled in (removable,
    // non-system are the flash candidates). One platform call, whole tree.
    virtual Result<DriveList> listDrives() = 0;

    // Mount a partition and hand back an RAII handle to its filesystem path. An
    // already-mounted partition is borrowed (left untouched on destruction);
    // otherwise it is mounted to a private temp dir and cleaned up on destruction.
    virtual Result<MountedPartition> mount(const Partition& partition) = 0;

    // Unmount every mounted partition of `node` (non-lazy) and swapoff any swap
    // on it. A live holder (md/LVM/LUKS) that cannot be released => DeviceBusy.
    virtual Result<void> unmountAll(const std::string& node) = 0;

    // Open the device for writing (exclusive) and hand back an owning IRawDevice
    // handle whose lifetime is the open session. Beats the udev auto-remount race
    // with a bounded retry loop. See docs: platform-backends.
    virtual Result<std::unique_ptr<IRawDevice>> openForWrite(
        const std::string& node) = 0;

    // Open the device read-only (exclusive) for the verify pass, returning an
    // owning IRawDevice handle.
    virtual Result<std::unique_ptr<IRawDevice>> openForRead(
        const std::string& node) = 0;
};

// Factory: returns the IDriveBackend for the host platform. Defined per-OS so
// only the current platform's implementation is compiled/linked.
std::unique_ptr<IDriveBackend> makeDriveBackend();

}  // namespace kuiper
