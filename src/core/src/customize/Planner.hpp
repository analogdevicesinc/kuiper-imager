#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "kuiper/Error.hpp"
#include "kuiper/ImageCustomizer.hpp"

// Pure planning helpers for ImageCustomizer: no I/O, no privilege, no platform
// calls — trivially unit-testable and shared by the Linux executor and --dry-run.
// See docs: customize-pipeline.
namespace kuiper::customize {

// Resolve catalog ids to their FeatureDefs, in catalog order, de-duplicated. An
// unknown id is an error (NotFound) listing the valid ids. An empty id list is an
// error too — customize with nothing selected is a mistake, not a no-op.
Result<std::vector<FeatureDef>> resolveFeatures(
    const std::vector<std::string>& ids);

// Bytes to grow the image/rootfs by: `override_` if set (0 => no grow), else
// autoGrowBytes() over the selected features' estimates. autoGrowBytes/parseSize
// are declared with the front-end-facing helpers in kuiper/ImageCustomizer.hpp.
std::uint64_t growBytesFor(const std::vector<FeatureDef>& features,
                           std::optional<std::uint64_t> override_);

// The argv for installing one feature's packages in the image chroot at
// `rootMount` ("" => a "<rootfs>" placeholder, for --dry-run). Empty if the
// feature has no packages.
std::vector<std::string> aptInstallArgv(const std::string& rootMount,
                                        const FeatureDef& feature);

// The argv for running a chroot shell snippet at `rootMount`.
std::vector<std::string> chrootShellArgv(const std::string& rootMount,
                                         const std::string& snippet);

// Keep only features whose `archs` is empty or contains `arch`.
std::vector<FeatureDef> filterByArch(const std::vector<FeatureDef>& features,
                                     const std::string& arch);

}  // namespace kuiper::customize
