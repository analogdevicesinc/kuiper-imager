#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace kuiper::test {

namespace fs = std::filesystem;

// An RAII temp directory, removed on destruction.
class TempDir {
public:
    TempDir() {
        auto base = fs::temp_directory_path();
        for (int i = 0;; ++i) {
            path_ = base / ("kuiper-test-" + std::to_string(counter_++) + "-" +
                            std::to_string(i));
            std::error_code ec;
            if (fs::create_directory(path_, ec)) break;
        }
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }
    fs::path file(const std::string& name) const { return path_ / name; }

private:
    fs::path path_;
    static inline int counter_ = 0;
};

// A deterministic byte pattern of `size` bytes. The seed avoids any gzip/xz/zip
// magic prefix so libarchive treats the output as a raw image and reads it back
// verbatim, rather than trying to decompress it.
inline std::vector<std::byte> makeImageBytes(std::size_t size) {
    std::vector<std::byte> out(size);
    std::uint32_t x = 0x1234567u;
    for (std::size_t i = 0; i < size; ++i) {
        x = x * 1664525u + 1013904223u;
        out[i] = static_cast<std::byte>((x >> 16) & 0xFF);
    }
    return out;
}

inline std::string writeBytes(const fs::path& path,
                              const std::vector<std::byte>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    return path.string();
}

inline std::string writeText(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    return path.string();
}

}  // namespace kuiper::test
