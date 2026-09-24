// Root-only Linux integration test: flash a real image through the actual
// LinuxDriveBackend / LinuxRawDevice over a loop device, then read the device back
// and compare. Registered under the CTest label `integration`; the case no-ops
// (passes) when not run as root so the default `ctest` run is green everywhere.
//
// Run it for real with:  sudo ctest --test-dir build -L integration --output-on-failure

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "kuiper/DriveService.hpp"

#include "support.hpp"

using namespace kuiper;

namespace {

// Attaches `backingFile` to a loop device and detaches it on destruction.
class LoopDevice {
public:
    explicit LoopDevice(const std::string& backingFile) {
        const std::string cmd =
            "losetup --find --show '" + backingFile + "' 2>/dev/null";
        FILE* pipe = ::popen(cmd.c_str(), "r");
        if (!pipe) return;
        std::array<char, 256> buf{};
        if (::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
            node_ = buf.data();
            while (!node_.empty() && (node_.back() == '\n' || node_.back() == ' '))
                node_.pop_back();
        }
        ::pclose(pipe);
    }
    ~LoopDevice() {
        if (!node_.empty()) {
            const std::string cmd = "losetup -d '" + node_ + "' 2>/dev/null";
            [[maybe_unused]] int rc = std::system(cmd.c_str());
        }
    }
    LoopDevice(const LoopDevice&) = delete;
    LoopDevice& operator=(const LoopDevice&) = delete;

    const std::string& node() const { return node_; }

private:
    std::string node_;
};

// Read exactly `length` bytes from the start of a device node.
std::vector<std::byte> readDevice(const std::string& node, std::size_t length) {
    std::vector<std::byte> out(length);
    int fd = ::open(node.c_str(), O_RDONLY);
    REQUIRE(fd >= 0);
    std::size_t got = 0;
    while (got < length) {
        ssize_t n = ::read(fd, out.data() + got, length - got);
        if (n <= 0) break;
        got += static_cast<std::size_t>(n);
    }
    ::close(fd);
    out.resize(got);
    return out;
}

}  // namespace

TEST_CASE("flash round-trips through the real backend on a loop device") {
    if (::geteuid() != 0) {
        MESSAGE("skipping: requires root (losetup). Run under `sudo ctest -L integration`.");
        return;
    }

    kuiper::test::TempDir tmp;

    // A 16 MiB sparse backing file for the loop device.
    const std::string backing = (tmp.path() / "backing.img").string();
    {
        int fd = ::open(backing.c_str(), O_CREAT | O_RDWR, 0600);
        REQUIRE(fd >= 0);
        REQUIRE(::ftruncate(fd, 16 << 20) == 0);
        ::close(fd);
    }

    LoopDevice loop(backing);
    if (loop.node().empty()) {
        // No free loop device (e.g. an unprivileged container). Skip rather than
        // fail — this is an environment limitation, not a defect. CI runners have
        // working loop devices, so the real path is still exercised there.
        MESSAGE("skipping: could not attach a loop device (losetup unavailable).");
        return;
    }

    // An 8 MiB raw image (block-aligned; exercises the O_DIRECT write path).
    const auto image = kuiper::test::makeImageBytes(8 << 20);
    const std::string imgPath =
        kuiper::test::writeBytes(tmp.path() / "kuiper.img", image);

    // A loop device is non-removable, so --force is required past the tier-2 guard.
    DriveService svc;
    auto r = svc.flash(loop.node(), imgPath, {.force = true});
    REQUIRE_MESSAGE(r, (r ? "" : r.error().message));
    CHECK(r->bytesWritten == image.size());

    const auto readBack = readDevice(loop.node(), image.size());
    REQUIRE(readBack.size() == image.size());
    CHECK(readBack == image);
}
