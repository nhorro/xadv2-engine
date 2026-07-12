#include "engine/core/resource_source.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

using namespace pac::core;

TEST_CASE("logical path validation accepts clean relative paths") {
    CHECK(is_valid_logical_path("a"));
    CHECK(is_valid_logical_path("backgrounds/study.png"));
    CHECK(is_valid_logical_path("a/b/c.txt"));
}

TEST_CASE("logical path validation rejects unsafe paths") {
    CHECK_FALSE(is_valid_logical_path(""));
    CHECK_FALSE(is_valid_logical_path("/absolute"));
    CHECK_FALSE(is_valid_logical_path("a\\b"));      // backslash
    CHECK_FALSE(is_valid_logical_path("..\\win"));   // backslash + ..
    CHECK_FALSE(is_valid_logical_path("../escape")); // parent
    CHECK_FALSE(is_valid_logical_path("a/../b"));    // parent segment
    CHECK_FALSE(is_valid_logical_path("a/./b"));     // current segment
    CHECK_FALSE(is_valid_logical_path("a//b"));      // empty segment
    CHECK_FALSE(is_valid_logical_path("trailing/")); // trailing slash
    CHECK_FALSE(is_valid_logical_path("C:/x"));      // drive
}

TEST_CASE("logical_join resolves relative to the naming file") {
    CHECK(logical_join("rooms", "b/background.png") == "rooms/b/background.png");
    CHECK(logical_join("", "background.png") == "background.png");
}

TEST_CASE("logical_join treats a leading slash as root-relative") {
    // The escape hatch for assets shared across directories: a room under
    // rooms/ naming a background under backgrounds/. Without it the join would
    // yield 'rooms//backgrounds/...', which is not a valid logical path.
    CHECK(logical_join("rooms", "/backgrounds/interior.png") == "backgrounds/interior.png");
    CHECK(logical_join("", "/backgrounds/interior.png") == "backgrounds/interior.png");
    CHECK(is_valid_logical_path(logical_join("rooms", "/backgrounds/interior.png")));
}

TEST_CASE("filesystem resource source reads, reports existence, and validates") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "xadv2_res_test";
    fs::remove_all(root);
    fs::create_directories(root / "sub");
    { std::ofstream(root / "sub" / "a.txt") << "hello"; }

    FilesystemResourceSource res(root.string());
    CHECK(res.exists("sub/a.txt"));
    CHECK_FALSE(res.exists("sub/missing.txt"));
    CHECK_FALSE(res.exists("../escape")); // invalid path -> not existing
    CHECK(res.read_text("sub/a.txt") == "hello");
    CHECK_THROWS_AS(res.read_text("missing.txt"), ResourceError);
    CHECK_THROWS_AS(res.host_path("../escape"), ResourceError);

    fs::remove_all(root);
}
