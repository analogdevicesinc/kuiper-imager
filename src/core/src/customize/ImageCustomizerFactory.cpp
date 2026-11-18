#include <memory>
#include <utility>

#include "kuiper/ImageCustomizer.hpp"

namespace kuiper {

#ifdef __linux__

// Defined in LinuxImageCustomizer.cpp (compiled only on Linux).
std::unique_ptr<ImageCustomizer> makeLinuxImageCustomizer(
    std::unique_ptr<CommandRunner> runner);

std::unique_ptr<ImageCustomizer> makeImageCustomizer(
    std::unique_ptr<CommandRunner> runner) {
    return makeLinuxImageCustomizer(std::move(runner));
}

#else

namespace {
class UnsupportedImageCustomizer : public ImageCustomizer {
public:
    Result<CustomizeSummary> customize(const CustomizePlan&, ProgressFn,
                                       CancelToken) override {
        return Err(ErrorCode::UnsupportedPlatform,
                   "Image customization is only supported on Linux",
                   "It relies on loop devices, chroot and binfmt.");
    }
};
}  // namespace

std::unique_ptr<ImageCustomizer> makeImageCustomizer(
    std::unique_ptr<CommandRunner> runner) {
    (void)runner;
    return std::make_unique<UnsupportedImageCustomizer>();
}

#endif

}  // namespace kuiper
