#include "engine/core/pack_resource_source.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace pac::core {

namespace {

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t read_u64_le(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    }
    return v;
}

std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

} // namespace

PackResourceSource::PackResourceSource(std::filesystem::path path) : path_(std::move(path)) {
    stream_.open(path_, std::ios::binary);
    if (!stream_) {
        throw ResourceError("pack: cannot open '" + path_.string() + "'");
    }

    std::uint8_t header[pack::kHeaderBytes];
    stream_.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!stream_ || static_cast<std::size_t>(stream_.gcount()) != sizeof(header)) {
        throw ResourceError("pack: '" + path_.string() + "' is shorter than the header");
    }
    if (std::memcmp(header, pack::kMagic.data(), pack::kMagic.size()) != 0) {
        throw ResourceError("pack: '" + path_.string() + "' has wrong magic (not a PAC1 archive)");
    }
    const std::uint32_t version = read_u32_le(header + 4);
    if (version != pack::kVersion) {
        throw ResourceError("pack: '" + path_.string() + "' has unsupported version " +
                            std::to_string(version));
    }
    const std::uint32_t toc_count = read_u32_le(header + 8);
    const std::uint64_t toc_offset = read_u64_le(header + 12);
    std::memcpy(seed_.data(), header + 20, pack::kSeedBytes);

    // Load the whole TOC region in one shot. Each entry is variable-length
    // (path_len + path + offset + size) — we walk linearly through the buffer.
    stream_.seekg(0, std::ios::end);
    const auto file_size = stream_.tellg();
    if (file_size < 0) {
        throw ResourceError("pack: '" + path_.string() + "' tellg failed");
    }
    if (toc_offset >= static_cast<std::uint64_t>(file_size)) {
        throw ResourceError("pack: '" + path_.string() + "' toc_offset is past end of file");
    }
    const std::size_t toc_size =
        static_cast<std::size_t>(file_size) - static_cast<std::size_t>(toc_offset);
    std::vector<std::uint8_t> toc_buf(toc_size);
    stream_.seekg(static_cast<std::streamoff>(toc_offset));
    stream_.read(reinterpret_cast<char*>(toc_buf.data()), static_cast<std::streamsize>(toc_size));
    if (static_cast<std::size_t>(stream_.gcount()) != toc_size) {
        throw ResourceError("pack: '" + path_.string() + "' TOC read truncated");
    }

    std::size_t cursor = 0;
    for (std::uint32_t i = 0; i < toc_count; ++i) {
        if (cursor + 2 > toc_buf.size()) {
            throw ResourceError("pack: '" + path_.string() + "' TOC truncated at entry " +
                                std::to_string(i));
        }
        const std::uint16_t plen = read_u16_le(toc_buf.data() + cursor);
        cursor += 2;
        if (plen == 0 || plen > pack::kMaxPathLen || cursor + plen + 16 > toc_buf.size()) {
            throw ResourceError("pack: '" + path_.string() + "' malformed TOC entry " +
                                std::to_string(i));
        }
        std::string logical(reinterpret_cast<const char*>(toc_buf.data() + cursor), plen);
        cursor += plen;
        const std::uint64_t file_off = read_u64_le(toc_buf.data() + cursor);
        cursor += 8;
        const std::uint64_t file_sz = read_u64_le(toc_buf.data() + cursor);
        cursor += 8;
        // Each payload must live fully before the TOC region (sanity).
        if (file_off + file_sz > toc_offset) {
            throw ResourceError("pack: '" + path_.string() + "' entry '" + logical +
                                "' payload overlaps the TOC");
        }
        toc_.emplace(std::move(logical), Entry{file_off, file_sz});
    }
}

void PackResourceSource::raw_read(std::uint64_t offset, std::size_t size, std::uint8_t* dst) const {
    std::lock_guard<std::mutex> guard(io_mutex_);
    // Clear EOF / fail bits from a prior end-of-stream seekg in the constructor
    // (otherwise the next seekg silently no-ops on some implementations).
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset));
    stream_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(size));
    if (!stream_ || static_cast<std::size_t>(stream_.gcount()) != size) {
        throw ResourceError("pack: '" + path_.string() + "' truncated read at offset " +
                            std::to_string(offset));
    }
}

std::vector<std::byte> PackResourceSource::read_entry(const std::string& logical) const {
    const auto it = toc_.find(logical);
    if (it == toc_.end()) {
        throw ResourceError("pack: '" + logical + "' not in archive '" + path_.string() + "'");
    }
    const Entry& e = it->second;
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(e.size));
    if (e.size > 0) {
        raw_read(e.offset, raw.size(), raw.data());
        pack::xor_with_keystream(seed_, logical, /*base_index=*/0, raw.data(), raw.size());
    }
    std::vector<std::byte> out(raw.size());
    std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

bool PackResourceSource::exists(const std::string& logical) const {
    return toc_.find(logical) != toc_.end();
}

std::string PackResourceSource::read_text(const std::string& logical) const {
    const std::vector<std::byte> bytes = read_entry(logical);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<std::byte> PackResourceSource::read_bytes(const std::string& logical) const {
    return read_entry(logical);
}

std::vector<std::string> PackResourceSource::list(const std::string& prefix,
                                                  const std::string& suffix) const {
    std::vector<std::string> out;
    // Treat `prefix` as a logical directory: match `prefix/` or empty. An empty
    // prefix matches every entry (subject to the suffix filter).
    const std::string needle = prefix.empty() ? std::string() : prefix + "/";
    for (const auto& [path, entry] : toc_) {
        if (!needle.empty() && path.compare(0, needle.size(), needle) != 0) {
            continue;
        }
        if (!suffix.empty() &&
            (path.size() < suffix.size() ||
             path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0)) {
            continue;
        }
        out.push_back(path);
    }
    // `toc_` is a `std::map`, so iteration is already lex-sorted; no extra
    // sort needed.
    return out;
}

} // namespace pac::core
