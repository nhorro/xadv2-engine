#include "engine/core/manifest.hpp"

#include <doctest/doctest.h>

using namespace pac::core;

namespace {

const char* kValid = R"YAML(
version: 1
resolution: { width: 1280, height: 720 }
window: { fullscreen: false, width: 1280, height: 720 }
resources: { src: "." }
strings: strings/es.yaml
settings:
  audio: { music_volume: 0.8, sfx_volume: 0.5 }
development:
  allow_room_reload: true
entry: title
scenes:
  - id: title
    type: TitleScreen
    parameters:
      new_game: gameplay
      exit: QUIT
  - id: gameplay
    type: Blank
)YAML";

} // namespace

TEST_CASE("valid manifest parses with expected fields") {
    Manifest m = parse_manifest(kValid);
    CHECK(m.version == 1);
    CHECK(m.resolution.x == 1280u);
    CHECK(m.resolution.y == 720u);
    CHECK(m.resources_src == ".");
    CHECK(m.strings_path == "strings/es.yaml");
    CHECK(m.settings.music_volume == doctest::Approx(0.8f));
    CHECK(m.settings.sfx_volume == doctest::Approx(0.5f));
    CHECK(m.development.allow_room_reload == true);
    CHECK(m.entry == "title");
    REQUIRE(m.scenes.size() == 2);

    const SceneDesc* title = m.find_scene("title");
    REQUIRE(title != nullptr);
    CHECK(title->type == "TitleScreen");
    CHECK(title->parameters.get_or("new_game", "") == "gameplay");
    CHECK(title->parameters.get_or("exit", "") == "QUIT");
    CHECK(m.find_scene("nope") == nullptr);
}

TEST_CASE("missing required fields throw ManifestError") {
    CHECK_THROWS_AS(parse_manifest("window: { width: 1 }\nresources: { src: . }\n"
                                   "strings: s\nentry: a\nscenes: [{id: a, type: B}]\n"),
                    ManifestError); // no resolution
    CHECK_THROWS_AS(parse_manifest("resolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nentry: a\n"
                                   "scenes: [{id: a, type: B}]\n"),
                    ManifestError); // no strings
    CHECK_THROWS_AS(parse_manifest("resolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "strings: s\nentry: a\nscenes: [{id: a, type: B}]\n"),
                    ManifestError); // no resources.src
}

TEST_CASE("zero resolution is rejected") {
    CHECK_THROWS_AS(parse_manifest("resolution: { width: 0, height: 720 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: a\n"
                                   "scenes: [{id: a, type: B}]\n"),
                    ManifestError);
}

TEST_CASE("duplicate scene ids are rejected") {
    CHECK_THROWS_AS(parse_manifest("resolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: a\n"
                                   "scenes: [{id: a, type: B}, {id: a, type: C}]\n"),
                    ManifestError);
}

TEST_CASE("entry must reference an existing scene") {
    CHECK_THROWS_AS(parse_manifest("resolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: missing\n"
                                   "scenes: [{id: a, type: B}]\n"),
                    ManifestError);
}

TEST_CASE("empty scenes list is rejected") {
    CHECK_THROWS_AS(parse_manifest("resolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: a\nscenes: []\n"),
                    ManifestError);
}
