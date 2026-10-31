#pragma once

#include <QString>
#include <cstdint>

// Small shared formatting helpers for the GUI (byte sizes, etc.).
namespace kuiper::gui {

// Human-readable size, matching the imager's "8.0 GB" / "1.0 TB" style.
inline QString formatBytes(std::uint64_t bytes) {
    constexpr double kib = 1024.0;
    double v = static_cast<double>(bytes);
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int i = 0;
    while (v >= kib && i < 5) {
        v /= kib;
        ++i;
    }
    return QString::number(v, 'f', (i == 0) ? 0 : 1) + " " + units[i];
}

}  // namespace kuiper::gui
