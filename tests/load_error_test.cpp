#include "engine/core/load_error.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace pac::core;

TEST_CASE("LoadError renders the full envelope on one line") {
    const LoadError e("room-loader",
                      "room.id-mismatch",
                      "id does not match filename",
                      SourceLocation{.file = "rooms/study.yaml", .line = 2, .column = 5});
    CHECK(e.source() == "room-loader");
    CHECK(e.code() == "room.id-mismatch");
    CHECK(e.message() == "id does not match filename");
    CHECK(e.location().file == "rooms/study.yaml");
    CHECK(e.location().line == 2);
    CHECK(e.location().column == 5);

    const std::string s = e.what();
    CHECK(s.find("[room-loader]") != std::string::npos);
    CHECK(s.find("rooms/study.yaml:2:5") != std::string::npos);
    CHECK(s.find("(room.id-mismatch)") != std::string::npos);
    CHECK(s.find("id does not match filename") != std::string::npos);
}

TEST_CASE("LoadError with no location renders source, code, and message only") {
    const LoadError e("strings-loader", "strings.defaults-missing", "no defaults block");
    CHECK(std::string(e.what()) == "[strings-loader] (strings.defaults-missing) no defaults block");
}

TEST_CASE("LoadError line-only location (no file) is still shown") {
    const LoadError e("cast-loader",
                      "cast.character-appearance-missing",
                      "missing appearance",
                      SourceLocation{.file = "", .line = 7, .column = 3});
    CHECK(std::string(e.what()).find("line 7") != std::string::npos);
}

TEST_CASE("with_file enriches the location and re-renders what()") {
    LoadError e("manifest-loader", "manifest.id-missing", "'id' is required");
    CHECK(std::string(e.what()).find("game.yaml") == std::string::npos);

    e.with_file("game.yaml");
    CHECK(e.location().file == "game.yaml");
    CHECK(std::string(e.what()).find("game.yaml") != std::string::npos);
}

TEST_CASE("the fail() helper throws a LoadError carrying the envelope") {
    try {
        fail("anim-loader", "anim.sequences-not-map", "boom");
        FAIL("expected fail() to throw");
    } catch (const LoadError& e) {
        CHECK(e.source() == "anim-loader");
        CHECK(e.code() == "anim.sequences-not-map");
        CHECK(e.message() == "boom");
    }
}
