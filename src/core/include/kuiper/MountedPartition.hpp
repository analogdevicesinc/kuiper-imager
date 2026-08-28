#pragma once

#include <functional>
#include <string>
#include <utility>

namespace kuiper {

// RAII handle to a mounted partition, readable at `path()`. It unmounts ONLY what
// Kuiper Imager itself mounted: an already-mounted partition is "borrowed" (no
// unmounter, left alone on destruction). Move-only; the backend's mount() is the
// sole producer.
class MountedPartition {
public:
    MountedPartition() = default;

    // `unmount` runs once on destruction (or move-assignment over this handle).
    // Pass an empty std::function for a borrowed mount that must not be touched.
    MountedPartition(std::string path, std::function<void()> unmount)
        : path_(std::move(path)), unmount_(std::move(unmount)) {}

    ~MountedPartition() { reset(); }

    MountedPartition(MountedPartition&& o) noexcept
        : path_(std::move(o.path_)), unmount_(std::move(o.unmount_)) {
        o.unmount_ = nullptr;
    }

    MountedPartition& operator=(MountedPartition&& o) noexcept {
        if (this != &o) {
            reset();
            path_ = std::move(o.path_);
            unmount_ = std::move(o.unmount_);
            o.unmount_ = nullptr;
        }
        return *this;
    }

    MountedPartition(const MountedPartition&) = delete;
    MountedPartition& operator=(const MountedPartition&) = delete;

    // Filesystem path where the partition is mounted (empty on a default handle).
    const std::string& path() const noexcept { return path_; }

    // True if this handle owns the mount (will unmount on destruction), false
    // for a borrowed / default handle.
    bool owned() const noexcept { return static_cast<bool>(unmount_); }

private:
    void reset() {
        if (unmount_) {
            unmount_();
            unmount_ = nullptr;
        }
    }

    std::string path_;
    std::function<void()> unmount_;  // empty => borrowed (already mounted)
};

}  // namespace kuiper
