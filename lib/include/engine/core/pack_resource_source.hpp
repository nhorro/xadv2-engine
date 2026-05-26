#pragma once

#include "engine/core/pack_format.hpp"
#include "engine/core/resource_source.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace pac::core {

/// A `ResourceSource` backed by a `.pak` archive (issue #109). Format and the
/// keystream derivation live in [pack_format.hpp]. The TOC is read once at
/// construction; payloads are read + de-obfuscated lazily per request and the
/// caller's cache (e.g. `ResourceCache`) keeps the decoded bytes.
///
/// `host_path` is intentionally not exposed — assets inside the archive have no
/// host path. The dev-only callers that needed it (room enumeration) use
/// `list()` instead, which works the same way for both backends.
class PackResourceSource : public ResourceSource {
public:
    /// Open `path` and parse the TOC. Throws `ResourceError` on a missing /
    /// truncated / malformed file or an unsupported `version`.
    explicit PackResourceSource(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    bool exists(const std::string& logical) const override;
    std::string read_text(const std::string& logical) const override;
    std::vector<std::byte> read_bytes(const std::string& logical) const override;
    [[nodiscard]] std::vector<std::string> list(const std::string& prefix,
                                                const std::string& suffix) const override;

private:
    struct Entry {
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    /// Read raw bytes from the open file handle (no de-obfuscation), guarded by
    /// `io_mutex_` because the file handle's seek+read pair is not thread-safe.
    void raw_read(std::uint64_t offset, std::size_t size, std::uint8_t* dst) const;

    /// Whole-file read + de-obfuscate by logical path. Throws on a missing
    /// entry or a truncated read. The result is owned by the caller.
    [[nodiscard]] std::vector<std::byte> read_entry(const std::string& logical) const;

    std::filesystem::path path_;
    mutable std::ifstream stream_;
    mutable std::mutex io_mutex_;
    std::array<std::uint8_t, pack::kSeedBytes> seed_{};
    std::map<std::string, Entry> toc_;
};

} // namespace pac::core
