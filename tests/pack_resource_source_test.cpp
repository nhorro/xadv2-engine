// PackResourceSource (issue #109). Headless round-trip: write a tiny pak by
// hand with the documented binary layout, open it through the runtime backend,
// and check that read_text / read_bytes / exists / list all agree with the
// inputs. The format spec lives in `pack_format.hpp`; this test pins it.

#include "engine/core/pack_format.hpp"
#include "engine/core/pack_resource_source.hpp"
#include "engine/core/resource_source.hpp"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using pac::core::FilesystemResourceSource;
using pac::core::PackResourceSource;
using pac::core::ResourceError;
using pac::core::ResourceSource;
namespace pack = pac::core::pack;

namespace {

void write_u16_le(std::ofstream& out, std::uint16_t v) {
    std::uint8_t b[2] = {static_cast<std::uint8_t>(v & 0xFF),
                         static_cast<std::uint8_t>((v >> 8) & 0xFF)};
    out.write(reinterpret_cast<const char*>(b), 2);
}

void write_u32_le(std::ofstream& out, std::uint32_t v) {
    std::uint8_t b[4];
    for (int i = 0; i < 4; ++i) {
        b[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF);
    }
    out.write(reinterpret_cast<const char*>(b), 4);
}

void write_u64_le(std::ofstream& out, std::uint64_t v) {
    std::uint8_t b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF);
    }
    out.write(reinterpret_cast<const char*>(b), 8);
}

/// Build a pak file with the given entries, mirroring tools/pack/pack.py.
/// Returns the host path so the test can pass it to PackResourceSource.
std::filesystem::path write_test_pak(const std::filesystem::path& dir,
                                     const std::vector<std::pair<std::string, std::string>>& files,
                                     std::array<std::uint8_t, pack::kSeedBytes> seed) {
    std::filesystem::create_directories(dir);
    const auto path = dir / "test.pak";
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out);

    // Header (patched at the end except seed; we just write the magic, version,
    // and zero-init the count + offset for now, then come back).
    out.write(pack::kMagic.data(), pack::kMagic.size());
    write_u32_le(out, pack::kVersion);
    write_u32_le(out, static_cast<std::uint32_t>(files.size()));
    write_u64_le(out, 0); // toc_offset (patched below)
    out.write(reinterpret_cast<const char*>(seed.data()), seed.size());

    struct Entry {
        std::uint64_t offset;
        std::uint64_t size;
    };
    std::vector<Entry> table;
    table.reserve(files.size());

    // Payloads.
    for (const auto& [logical, content] : files) {
        std::vector<std::uint8_t> bytes(content.begin(), content.end());
        pack::xor_with_keystream(seed, logical, 0, bytes.data(), bytes.size());
        const auto off = static_cast<std::uint64_t>(out.tellp());
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        table.push_back({off, static_cast<std::uint64_t>(content.size())});
    }

    const auto toc_off = static_cast<std::uint64_t>(out.tellp());
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::string& logical = files[i].first;
        write_u16_le(out, static_cast<std::uint16_t>(logical.size()));
        out.write(logical.data(), static_cast<std::streamsize>(logical.size()));
        write_u64_le(out, table[i].offset);
        write_u64_le(out, table[i].size);
    }

    // Patch the toc_offset field.
    out.seekp(12, std::ios::beg);
    write_u64_le(out, toc_off);
    out.close();
    return path;
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("pac_pak_test_" + std::to_string(now));
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

} // namespace

TEST_CASE("PackResourceSource round-trips text + binary entries") {
    TempDir td;
    std::array<std::uint8_t, pack::kSeedBytes> seed{};
    for (std::size_t i = 0; i < seed.size(); ++i) {
        seed[i] = static_cast<std::uint8_t>(0x42 + i);
    }
    const auto pak = write_test_pak(td.path,
                                    {
                                        {"game.yaml", "id: theheadless\n"},
                                        {"scripts/intro.lua", "print('hi')"},
                                        {"data/bin", std::string("\x00\x01\xFE\xFF", 4)},
                                    },
                                    seed);

    PackResourceSource src(pak);

    CHECK(src.exists("game.yaml"));
    CHECK(src.exists("scripts/intro.lua"));
    CHECK_FALSE(src.exists("nope"));

    CHECK(src.read_text("game.yaml") == "id: theheadless\n");
    CHECK(src.read_text("scripts/intro.lua") == "print('hi')");

    const auto bytes = src.read_bytes("data/bin");
    REQUIRE(bytes.size() == 4);
    CHECK(static_cast<std::uint8_t>(bytes[0]) == 0x00u);
    CHECK(static_cast<std::uint8_t>(bytes[3]) == 0xFFu);
}

TEST_CASE("PackResourceSource::list filters by prefix and suffix") {
    TempDir td;
    std::array<std::uint8_t, pack::kSeedBytes> seed{};
    const auto pak = write_test_pak(td.path,
                                    {
                                        {"rooms/exterior.yaml", "id: exterior\n"},
                                        {"rooms/hall.yaml", "id: hall\n"},
                                        {"rooms/study.yaml", "id: study\n"},
                                        {"rooms/study.lua", "-- behavior"},
                                        {"strings/es.yaml", "ui: {}\n"},
                                    },
                                    seed);

    PackResourceSource src(pak);

    const std::vector<std::string> yaml_rooms = src.list("rooms", ".yaml");
    CHECK(yaml_rooms ==
          std::vector<std::string>{"rooms/exterior.yaml", "rooms/hall.yaml", "rooms/study.yaml"});

    // Empty suffix = no extension filter, still scoped by prefix.
    CHECK(src.list("rooms", "").size() == 4);

    // Empty prefix = match everywhere; the suffix still narrows.
    CHECK(src.list("", ".yaml").size() == 4);

    // Nothing under an unknown prefix.
    CHECK(src.list("nope", ".yaml").empty());
}

TEST_CASE("PackResourceSource rejects malformed archives") {
    TempDir td;
    std::filesystem::create_directories(td.path);

    // Wrong magic.
    {
        const auto path = td.path / "bad_magic.pak";
        std::ofstream out(path, std::ios::binary);
        out << "XXXX";
        for (int i = 4; i < static_cast<int>(pack::kHeaderBytes); ++i) {
            out.put('\0');
        }
        out.close();
        CHECK_THROWS_AS(PackResourceSource{path}, ResourceError);
    }

    // Truncated (shorter than header).
    {
        const auto path = td.path / "short.pak";
        std::ofstream out(path, std::ios::binary);
        out.write("PAC1", 4);
        out.close();
        CHECK_THROWS_AS(PackResourceSource{path}, ResourceError);
    }

    // Unsupported version.
    {
        const auto path = td.path / "ver.pak";
        std::ofstream out(path, std::ios::binary);
        out.write(pack::kMagic.data(), pack::kMagic.size());
        write_u32_le(out, pack::kVersion + 1);
        write_u32_le(out, 0);  // toc_count
        write_u64_le(out, 36); // toc_offset = end of header
        for (std::size_t i = 0; i < pack::kSeedBytes; ++i) {
            out.put('\0');
        }
        out.close();
        CHECK_THROWS_AS(PackResourceSource{path}, ResourceError);
    }
}

TEST_CASE("PackResourceSource keystream is symmetric (write then read)") {
    // Sanity check that the deobfuscation undoes the obfuscation. The C++
    // packer doesn't exist (Python lives outside the C++ test suite), so use
    // xor_with_keystream as both encoder and decoder.
    std::array<std::uint8_t, pack::kSeedBytes> seed{};
    for (std::size_t i = 0; i < seed.size(); ++i) {
        seed[i] = static_cast<std::uint8_t>(i * 7 + 1);
    }

    const std::string path = "shaders/water.frag";
    std::vector<std::uint8_t> plain;
    for (int i = 0; i < 257; ++i) { // > 256 to cross the byte wraparound
        plain.push_back(static_cast<std::uint8_t>(i % 256));
    }
    auto data = plain;
    pack::xor_with_keystream(seed, path, 0, data.data(), data.size());
    REQUIRE(data != plain); // actually obfuscated
    pack::xor_with_keystream(seed, path, 0, data.data(), data.size());
    CHECK(data == plain);
}
