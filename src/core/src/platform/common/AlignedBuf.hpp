#pragma once

#include <cstddef>
#include <cstdlib>

#if defined(_WIN32)
#include <malloc.h>  // _aligned_malloc / _aligned_free
#endif

namespace kuiper::detail {

// Owns a block-aligned heap buffer. Direct/unbuffered device I/O requires the
// buffer, the file offset, and the transfer length to be block-aligned; this
// gives the backend the buffer half. Move-only; frees on scope exit. The
// allocator is the one platform detail here: posix_memalign on POSIX,
// _aligned_malloc on Windows.
class AlignedBuf {
public:
    AlignedBuf() = default;

    AlignedBuf(std::size_t size, std::size_t align) {
#if defined(_WIN32)
        void* p = ::_aligned_malloc(size, align);
        if (p) {
            p_ = static_cast<std::byte*>(p);
            n_ = size;
        }
#else
        void* p = nullptr;
        if (::posix_memalign(&p, align, size) == 0) {
            p_ = static_cast<std::byte*>(p);
            n_ = size;
        }
#endif
    }

    ~AlignedBuf() { release(); }

    AlignedBuf(AlignedBuf&& o) noexcept : p_(o.p_), n_(o.n_) {
        o.p_ = nullptr;
        o.n_ = 0;
    }
    AlignedBuf& operator=(AlignedBuf&& o) noexcept {
        if (this != &o) {
            release();
            p_ = o.p_;
            n_ = o.n_;
            o.p_ = nullptr;
            o.n_ = 0;
        }
        return *this;
    }

    AlignedBuf(const AlignedBuf&) = delete;
    AlignedBuf& operator=(const AlignedBuf&) = delete;

    std::byte* get() const noexcept { return p_; }
    std::size_t size() const noexcept { return n_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

private:
    void release() noexcept {
#if defined(_WIN32)
        if (p_) ::_aligned_free(p_);
#else
        std::free(p_);
#endif
    }

    std::byte* p_ = nullptr;
    std::size_t n_ = 0;
};

}  // namespace kuiper::detail
