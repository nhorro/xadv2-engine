#include "engine/core/pack_format.hpp"

namespace pac::core::pack {

std::uint32_t fnv1a_32(const std::string& s) {
    std::uint32_t h = 0x811c9dc5u;
    for (unsigned char c : s) {
        h ^= static_cast<std::uint32_t>(c);
        h *= 0x01000193u; // FNV prime
    }
    return h;
}

std::uint8_t keystream_byte(const std::array<std::uint8_t, kSeedBytes>& seed,
                            const std::string& path,
                            std::uint64_t i) {
    // Mix three sources: the global seed (16 bytes, archive-wide), an FNV-1a
    // hash of the logical path (so two files share no key), and the byte index
    // (so a run of identical plaintext bytes is not a run of identical
    // ciphertext bytes — defeats trivial pattern spotting).
    const std::uint32_t h = fnv1a_32(path);
    const std::uint8_t s = seed[(i + (h & 0xFu)) % kSeedBytes];
    const std::uint8_t hbyte = static_cast<std::uint8_t>((h >> ((i & 3u) * 8u)) & 0xFFu);
    const std::uint8_t cnt = static_cast<std::uint8_t>(i * 0x5Bu); // LCG-ish stir
    return static_cast<std::uint8_t>(s ^ hbyte ^ cnt);
}

void xor_with_keystream(const std::array<std::uint8_t, kSeedBytes>& seed,
                        const std::string& path,
                        std::uint64_t base_index,
                        std::uint8_t* data,
                        std::size_t len) {
    for (std::size_t k = 0; k < len; ++k) {
        data[k] = static_cast<std::uint8_t>(data[k] ^ keystream_byte(seed, path, base_index + k));
    }
}

} // namespace pac::core::pack
