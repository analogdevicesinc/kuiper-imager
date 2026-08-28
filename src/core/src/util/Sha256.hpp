#pragma once

#include <cstddef>
#include <string>

#include <openssl/evp.h>

namespace kuiper::detail {

// Streaming SHA-256 over OpenSSL's EVP interface (libcrypto uses the CPU's SHA
// extensions where present — far faster than Qt's software hash). Move-only RAII
// around EVP_MD_CTX; hex() finalizes and must be called at most once. Header-only
// so the flash and fetch paths share one implementation.
class Sha256 {
public:
    Sha256() : ctx_(EVP_MD_CTX_new()) {
        if (ctx_) EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr);
    }
    ~Sha256() { EVP_MD_CTX_free(ctx_); }
    Sha256(Sha256&& o) noexcept : ctx_(o.ctx_) { o.ctx_ = nullptr; }
    Sha256& operator=(Sha256&& o) noexcept {
        if (this != &o) {
            EVP_MD_CTX_free(ctx_);
            ctx_ = o.ctx_;
            o.ctx_ = nullptr;
        }
        return *this;
    }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void update(const std::byte* p, std::size_t n) {
        EVP_DigestUpdate(ctx_, p, n);
    }

    std::string hex() {
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned len = 0;
        EVP_DigestFinal_ex(ctx_, md, &len);
        static const char* kHex = "0123456789abcdef";
        std::string out;
        out.reserve(static_cast<std::size_t>(len) * 2);
        for (unsigned i = 0; i < len; ++i) {
            out.push_back(kHex[md[i] >> 4]);
            out.push_back(kHex[md[i] & 0x0f]);
        }
        return out;
    }

private:
    EVP_MD_CTX* ctx_;
};

}  // namespace kuiper::detail
