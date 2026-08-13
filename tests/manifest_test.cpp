#include "engine/core/manifest.hpp"
#include "engine/core/resource_source.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

using namespace pac::core;
using pac::test::error_code;

namespace {

class ManifestSource : public ResourceSource {
public:
    std::map<std::string, std::string> files;
    bool exists(const std::string& logical) const override { return files.contains(logical); }
    std::string read_text(const std::string& logical) const override {
        const auto it = files.find(logical);
        if (it == files.end())
            throw ResourceError("missing " + logical);
        return it->second;
    }
    std::vector<std::byte> read_bytes(const std::string&) const override { return {}; }
};

const char* kValid = R"YAML(
version: 1
id: sample
resolution: { width: 1280, height: 720 }
window: { fullscreen: false, width: 1280, height: 720 }
resources: { src: "." }
facts: chapters/01/facts.yaml
strings: strings/es.yaml
settings:
  audio: { music_volume: 0.8, sfx_volume: 0.5, speech_enabled: false }
speech:
  font: fonts/dialogue.ttf
  font_size: 30
  voice_directory: speech/es
development:
  allow_room_reload: true
  warn_missing_translations: true
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
    CHECK(m.id == "sample");
    CHECK(m.version == 1);
    CHECK(m.resolution.x == 1280u);
    CHECK(m.resolution.y == 720u);
    CHECK(m.rendering.smooth_textures);
    CHECK(m.resources_src == ".");
    CHECK(m.facts_path == "chapters/01/facts.yaml");
    CHECK(m.strings_path == "strings/es.yaml");
    CHECK(m.settings.music_volume == doctest::Approx(0.8f));
    CHECK(m.settings.sfx_volume == doctest::Approx(0.5f));
    CHECK_FALSE(m.settings.speech_enabled);
    CHECK(m.speech.font == "fonts/dialogue.ttf");
    CHECK(m.speech.font_size == 30u);
    CHECK(m.speech.voice_directory == "speech/es");
    CHECK(m.development.allow_room_reload == true);
    CHECK(m.development.warn_missing_translations);
    CHECK(m.entry == "title");
    REQUIRE(m.scenes.size() == 2);

    const SceneDesc* title = m.find_scene("title");
    REQUIRE(title != nullptr);
    CHECK(title->type == "TitleScreen");
    CHECK(title->parameters.get_or("new_game", "") == "gameplay");
    CHECK(title->parameters.get_or("exit", "") == "QUIT");
    CHECK(m.find_scene("nope") == nullptr);
}

TEST_CASE("facts path defaults to the resource root and rejects an empty override") {
    const Manifest defaults =
        parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                       "resources: { src: . }\nstrings: s\nentry: a\n"
                       "scenes: [{id: a, type: B}]\n");
    CHECK(defaults.facts_path == "facts.yaml");

    CHECK(error_code([] {
              parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                             "resources: { src: . }\nfacts: ''\nstrings: s\nentry: a\n"
                             "scenes: [{id: a, type: B}]\n");
          }) == "manifest.facts-empty");
}

TEST_CASE("linear chapters bind stable ids to restorable scenes and facts") {
    const Manifest m = parse_manifest(
        "id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
        "resources: { src: . }\nfacts: chapters/one/facts.yaml\nstrings: s\nentry: a\n"
        "scenes: [{id: a, type: RoomScene}, {id: b, type: RoomScene}]\n"
        "chapters:\n"
        "  - {id: one, scene: a}\n"
        "  - {id: two, scene: b, facts: chapters/two/facts.yaml}\n");

    REQUIRE(m.chapters.size() == 2);
    CHECK(m.chapters[0].facts_path == "chapters/one/facts.yaml");
    CHECK(m.chapters[1].facts_path == "chapters/two/facts.yaml");
    REQUIRE(m.chapter_for_scene("b") != nullptr);
    CHECK(m.chapter_for_scene("b")->id == "two");
    REQUIRE(m.next_chapter("one") != nullptr);
    CHECK(m.next_chapter("one")->id == "two");
    CHECK(m.next_chapter("two") == nullptr);
}

TEST_CASE("chapter declarations reject duplicates and unknown scenes") {
    const std::string prefix = "id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                               "resources: { src: . }\nstrings: s\nentry: a\n"
                               "scenes: [{id: a, type: RoomScene}]\nchapters:\n";

    CHECK(error_code([&] { parse_manifest(prefix + "  - {id: one, scene: missing}\n"); }) ==
          "manifest.chapter-scene-unknown");
    CHECK(error_code([&] {
              parse_manifest(prefix + "  - {id: one, scene: a}\n  - {id: one, scene: a}\n");
          }) == "manifest.duplicate-chapter-id");
}

TEST_CASE("texture smoothing defaults on, supports opt-out, and rendering must be a mapping") {
    const Manifest defaults =
        parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                       "resources: { src: . }\nstrings: s\nentry: a\n"
                       "scenes: [{id: a, type: B}]\n");
    CHECK(defaults.rendering.smooth_textures);

    const Manifest pixel_art =
        parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                       "rendering: { smooth_textures: false }\n"
                       "resources: { src: . }\nstrings: s\nentry: a\n"
                       "scenes: [{id: a, type: B}]\n");
    CHECK_FALSE(pixel_art.rendering.smooth_textures);

    CHECK(error_code([] {
              parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                             "rendering: linear\nresources: { src: . }\nstrings: s\nentry: a\n"
                             "scenes: [{id: a, type: B}]\n");
          }) == "manifest.rendering-not-map");
}

TEST_CASE("speech config is optional and validates its font size") {
    const Manifest defaults =
        parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                       "resources: { src: . }\nstrings: s\nentry: a\n"
                       "scenes: [{id: a, type: B}]\n");
    CHECK(defaults.speech.font.empty());
    CHECK(defaults.speech.font_size == 24u);

    CHECK_THROWS_AS(parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: a\n"
                                   "speech: { font_size: 0 }\n"
                                   "scenes: [{id: a, type: B}]\n"),
                    ManifestError);
    CHECK_THROWS_AS(parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: a\n"
                                   "speech: large\n"
                                   "scenes: [{id: a, type: B}]\n"),
                    ManifestError);
}

TEST_CASE("missing required fields throw ManifestError") {
    CHECK_THROWS_AS(parse_manifest("id: g\nwindow: { width: 1 }\nresources: { src: . }\n"
                                   "strings: s\nentry: a\nscenes: [{id: a, type: B}]\n"),
                    ManifestError); // no resolution
    CHECK_THROWS_AS(parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nentry: a\n"
                                   "scenes: [{id: a, type: B}]\n"),
                    ManifestError); // no strings
    CHECK_THROWS_AS(parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "strings: s\nentry: a\nscenes: [{id: a, type: B}]\n"),
                    ManifestError); // no resources.src
}

TEST_CASE("zero resolution is rejected") {
    CHECK_THROWS_AS(parse_manifest("id: g\nresolution: { width: 0, height: 720 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: a\n"
                                   "scenes: [{id: a, type: B}]\n"),
                    ManifestError);
}

TEST_CASE("duplicate scene ids are rejected") {
    CHECK_THROWS_AS(parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: a\n"
                                   "scenes: [{id: a, type: B}, {id: a, type: C}]\n"),
                    ManifestError);
}

TEST_CASE("entry must reference an existing scene") {
    CHECK_THROWS_AS(parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: missing\n"
                                   "scenes: [{id: a, type: B}]\n"),
                    ManifestError);
}

TEST_CASE("empty scenes list is rejected") {
    CHECK_THROWS_AS(parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: a\nscenes: []\n"),
                    ManifestError);
}

TEST_CASE("manifest id is required and validated") {
    // Missing.
    CHECK_THROWS_AS(parse_manifest("resolution: { width: 1, height: 1 }\nwindow: {}\n"
                                   "resources: { src: . }\nstrings: s\nentry: a\n"
                                   "scenes: [{id: a, type: B}]\n"),
                    ManifestError);
    // Path-unsafe characters (rejecting '/', '..', uppercase, spaces).
    auto with_id = [](const char* id) {
        return std::string("id: ") + id +
               "\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
               "resources: { src: . }\nstrings: s\nentry: a\n"
               "scenes: [{id: a, type: B}]\n";
    };
    CHECK_THROWS_AS(parse_manifest(with_id("foo/bar")), ManifestError);
    CHECK_THROWS_AS(parse_manifest(with_id("..")), ManifestError);
    CHECK_THROWS_AS(parse_manifest(with_id("FooBar")), ManifestError);
    CHECK_THROWS_AS(parse_manifest(with_id("foo bar")), ManifestError);
    // Valid: lowercase, digits, underscore, dash.
    CHECK_NOTHROW(parse_manifest(with_id("the_mummy-2")));
}

TEST_CASE("nested scene parameters flatten into dotted keys") {
    const Manifest m = parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                                      "resources: { src: . }\nstrings: s\nentry: a\n"
                                      "scenes:\n"
                                      "  - id: a\n"
                                      "    type: TitleScreen\n"
                                      "    parameters:\n"
                                      "      font_size: 12\n"
                                      "      menu:\n"
                                      "        position: { x: 0.5, y: 0.7 }\n"
                                      "        options:\n"
                                      "          new_game: intro\n"
                                      "          exit: QUIT\n");
    const SceneDesc* a = m.find_scene("a");
    REQUIRE(a != nullptr);
    CHECK(a->parameters.get_or("font_size", "") == "12");
    CHECK(a->parameters.get_or("menu.position.x", "") == "0.5");
    CHECK(a->parameters.get_or("menu.position.y", "") == "0.7");
    CHECK(a->parameters.get_or("menu.options.new_game", "") == "intro");
    CHECK(a->parameters.get_or("menu.options.exit", "") == "QUIT");

    SceneParams params;
    params.set("pause_menu.overlays.notebook.scene", "notebook");
    params.set("pause_menu.overlays.map.scene", "map");
    params.set("pause_menu.overlays.map.label_key", "map");
    CHECK(params.children("pause_menu.overlays") == std::vector<std::string>{"map", "notebook"});
}

TEST_CASE("optional cursor block is parsed") {
    const Manifest m =
        parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                       "resources: { src: . }\nstrings: s\nentry: a\nscenes: [{id: a, type: B}]\n"
                       "cursor:\n"
                       "  image: ui/c.png\n"
                       "  interact: ui/h.png\n"
                       "  hotspot: { x: 2, y: 3 }\n"
                       "  blink:\n"
                       "    interval: 0.35\n"
                       "    steps: 16\n"
                       "    dark: { r: 48, g: 49, b: 50 }\n"
                       "    light: { r: 253, g: 254, b: 255 }\n");
    CHECK(m.cursor.image == "ui/c.png");
    CHECK(m.cursor.interact == "ui/h.png");
    CHECK(m.cursor.hotspot.x == 2u);
    CHECK(m.cursor.hotspot.y == 3u);
    CHECK(m.cursor.blink.interval == doctest::Approx(0.35f));
    CHECK(m.cursor.blink.steps == 16u);
    CHECK(m.cursor.blink.dark == sf::Color(48, 49, 50));
    CHECK(m.cursor.blink.light == sf::Color(253, 254, 255));
}

TEST_CASE("cursor block is optional — defaults to no custom cursor") {
    const Manifest m = parse_manifest(kValid);
    CHECK(m.cursor.image.empty());
    CHECK(m.cursor.interact.empty());
    CHECK(m.cursor.hotspot.x == 0u);
    CHECK(m.cursor.hotspot.y == 0u);
    CHECK_FALSE(m.cursor.blink.enabled());
}

TEST_CASE("single-language shorthand yields one language entry") {
    const Manifest m = parse_manifest(kValid);
    REQUIRE(m.languages.size() == 1);
    CHECK(m.languages[0].strings_path == "strings/es.yaml");
    CHECK(m.default_language == m.languages[0].id);
    CHECK(m.strings_path == "strings/es.yaml");
}

TEST_CASE("languages list is parsed; default is the first entry") {
    const Manifest m =
        parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                       "resources: { src: . }\nentry: a\nscenes: [{id: a, type: B}]\n"
                       "languages:\n"
                       "  - { id: es, name: \"Español\", strings: strings/es.yaml }\n"
                       "  - { id: en, name: \"English\", strings: strings/en.yaml, translations: "
                       "translations/en.yaml }\n");
    REQUIRE(m.languages.size() == 2);
    CHECK(m.languages[0].id == "es");
    CHECK(m.languages[0].name == "Español");
    CHECK(m.languages[1].id == "en");
    CHECK(m.languages[1].translations_path == "translations/en.yaml");
    CHECK(m.default_language == "es");          // first entry
    CHECK(m.strings_path == "strings/es.yaml"); // default's strings
}

TEST_CASE("default_language overrides the first entry") {
    const Manifest m =
        parse_manifest("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                       "resources: { src: . }\nentry: a\nscenes: [{id: a, type: B}]\n"
                       "default_language: en\n"
                       "languages:\n"
                       "  - { id: es, strings: strings/es.yaml }\n"
                       "  - { id: en, strings: strings/en.yaml }\n");
    CHECK(m.default_language == "en");
    CHECK(m.strings_path == "strings/en.yaml");
    CHECK(m.languages[1].name == "en"); // name defaults to id when omitted
}

TEST_CASE("invalid languages declarations are rejected") {
    auto base = [](const char* tail) {
        return std::string("id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                           "resources: { src: . }\nentry: a\nscenes: [{id: a, type: B}]\n") +
               tail;
    };
    // Neither strings nor languages.
    CHECK_THROWS_AS(parse_manifest(base("")), ManifestError);
    // Duplicate language id.
    CHECK_THROWS_AS(parse_manifest(base("languages:\n  - { id: es, strings: a }\n"
                                        "  - { id: es, strings: b }\n")),
                    ManifestError);
    // Language missing its strings path.
    CHECK_THROWS_AS(parse_manifest(base("languages:\n  - { id: es }\n")), ManifestError);
    // default_language not in the list.
    CHECK_THROWS_AS(parse_manifest(base("default_language: fr\n"
                                        "languages:\n  - { id: es, strings: a }\n")),
                    ManifestError);
}

TEST_CASE("manifest diagnostics carry stable error codes") {
    const char* dup = "id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                      "resources: { src: . }\nstrings: s\nentry: a\n"
                      "scenes: [{id: a, type: B}, {id: a, type: C}]\n";
    CHECK(error_code([&] { parse_manifest(dup); }) == "manifest.duplicate-scene-id");

    const char* bad_entry = "id: g\nresolution: { width: 1, height: 1 }\nwindow: {}\n"
                            "resources: { src: . }\nstrings: s\nentry: missing\n"
                            "scenes: [{id: a, type: B}]\n";
    CHECK(error_code([&] { parse_manifest(bad_entry); }) == "manifest.entry-not-in-scenes");
}

TEST_CASE("version 2 composes chapter scenes defaults profiles and relative paths") {
    ManifestSource source;
    source.files["game.yaml"] = R"YAML(
version: 2
id: sample
resolution: { width: 1280, height: 720 }
window: {}
resources: { src: . }
strings: ./strings/es.yaml
entry: intro
scene_defaults:
  all: { font: ./shared/ui.ttf }
  RoomScene: { player: player, scumm_panel: ./shared/panel.yml }
scene_profiles:
  document:
    type: CloseUp
    music: ./shared/mystery.ogg
chapters: [./chapters/one/chapter.yaml]
scenes:
  - id: title
    type: TitleScreen
    background: ./campaign/title.png
)YAML";
    source.files["chapters/one/chapter.yaml"] = R"YAML(
version: 2
id: one
title: One
facts: ./facts.yaml
cast: ./cast.yaml
rooms: ./rooms
dialogs: ./dialogs
inventory: ./inventory/items.yaml
inventory_logic: ./inventory/items.lua
logic: ./scripts/game.lua
room: { id: room_one, start: study }
scene_defaults:
  CloseUp:
    cast: ./cast.yaml
    background_color: { r: 20, g: 16, b: 12 }
scenes:
  - id: intro
    type: Cutscene
    data: ./cutscenes/intro.yaml
    on_finish: room_one
  - id: letter
    profile: document
    source: ./closeups/letter
)YAML";
    source.files["chapters/one/closeups/letter/closeup.yml"] = "background: letter.png\n";
    source.files["chapters/one/closeups/letter/logic.lua"] = "return {}\n";

    const Manifest manifest = load_manifest(source, "game.yaml");
    CHECK(manifest.strings_path == "strings/es.yaml");
    CHECK(manifest.facts_path == "chapters/one/facts.yaml");
    REQUIRE(manifest.chapters.size() == 1);
    CHECK(manifest.chapters[0].scene == "room_one");

    const SceneDesc* room = manifest.find_scene("room_one");
    REQUIRE(room);
    CHECK(room->parameters.get_or("font", "") == "shared/ui.ttf");
    CHECK(room->parameters.get_or("cast", "") == "chapters/one/cast.yaml");
    CHECK(room->parameters.get_or("rooms", "") == "chapters/one/rooms");
    CHECK(room->parameters.get_or("start_room", "") == "study");
    CHECK(room->parameters.get_or("scumm_panel", "") == "shared/panel.yml");

    const SceneDesc* letter = manifest.find_scene("letter");
    REQUIRE(letter);
    CHECK(letter->type == "CloseUp");
    CHECK(letter->parameters.get_or("data", "") == "chapters/one/closeups/letter/closeup.yml");
    CHECK(letter->parameters.get_or("logic", "") == "chapters/one/closeups/letter/logic.lua");
    CHECK(letter->parameters.get_or("cast", "") == "chapters/one/cast.yaml");
    CHECK(letter->parameters.get_or("music", "") == "shared/mystery.ogg");
    CHECK(letter->parameters.get_or("background_color.r", "") == "20");
}
