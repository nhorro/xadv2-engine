#pragma once

// On-disk format for `.pak` resource archives (issue #109). One archive contains
// a flat collection of files keyed by logical resource path (the same paths the
// rest of the engine uses); the runtime backend is `PackResourceSource`. Both
// the C++ runtime and the Python packer in `nhorro/xadv2-tools/pack/` MUST agree on the
// values declared here, so this header is the canonical specification.
//
// On-disk layout (all integers little-endian):
//
//   off  size  field
//   ---  ----  --------------------------------------------------------
//    0    4    magic       ASCII "PAC1"
//    4    4    version     uint32 (= kVersion)
//    8    4    toc_count   uint32 — number of entries
//   12    8    toc_offset  uint64 — byte offset of the TOC table from the
//                          start of the file (the TOC lives at the END so the
//                          packer can stream file payloads then patch the
//                          header)
//   20   16    seed        16 random bytes used to derive the XOR keystream
//   36    .    data        concatenated obfuscated payloads (in any order)
//   .    .    TOC          `toc_count` entries (see below)
//
// Each TOC entry:
//   off  size  field
//   ---  ----  -----------------------------------
//    0    2    path_len    uint16 — UTF-8 bytes of the logical path
//    2    N    path        N bytes of UTF-8 (no null terminator)
//    .    8    file_offset uint64 — byte offset of the payload
//    .    8    file_size   uint64 — payload length in bytes
//
// The TOC itself is NOT obfuscated (so the runtime can read it without
// knowing the per-file key) and the seed is stored in the clear; the goal is
// obfuscation against casual snooping, not cryptography. Logical paths inside
// a TOC entry must be valid per `is_valid_logical_path`.
//
// Payload obfuscation: each file's bytes are XORed with a per-file keystream
// derived from the archive seed plus a hash of the path. See `keystream_byte`.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pac::core::pack {

inline constexpr std::array<char, 4> kMagic{'P', 'A', 'C', '1'};
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::size_t kSeedBytes = 16;
inline constexpr std::size_t kHeaderBytes =
    36; // magic(4) + ver(4) + count(4) + toc_off(8) + seed(16)
inline constexpr std::uint16_t kMaxPathLen = 1024;

/// FNV-1a 32-bit. Deterministic across platforms; the packer and runtime must
/// agree on the value for the same input.
[[nodiscard]] std::uint32_t fnv1a_32(const std::string& s);

/// Byte i of the keystream for a file at `path` in an archive with `seed`. The
/// keystream is cheap to compute and only needs to defeat a hex-editor reader.
/// The runtime XORs each read byte with this; the packer XORs on write.
[[nodiscard]] std::uint8_t keystream_byte(const std::array<std::uint8_t, kSeedBytes>& seed,
                                          const std::string& path,
                                          std::uint64_t i);

/// XOR `data` in place with the per-file keystream. Symmetric: apply once to
/// obfuscate, once to deobfuscate.
void xor_with_keystream(const std::array<std::uint8_t, kSeedBytes>& seed,
                        const std::string& path,
                        std::uint64_t base_index,
                        std::uint8_t* data,
                        std::size_t len);

} // namespace pac::core::pack
