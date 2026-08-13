#include "engine/pnc/closeup.hpp"
#include "engine/pnc/data_error.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;
using pac::test::error_code;

namespace {

const char* kCloseUp = R"YAML(
id: skull
background: closeups/skull.png
hotspots:
  jaw:
    name: "la mandíbula"
    area: [ {x: 0, y: 0}, {x: 100, y: 0}, {x: 100, y: 100}, {x: 0, y: 100} ]
  panel:
    name: "un panel"
    area: [ {x: 200, y: 200}, {x: 300, y: 200}, {x: 300, y: 300}, {x: 200, y: 300} ]
    goto: hall_map
)YAML";

} // namespace

TEST_CASE("parse_closeup reads background and hotspots with actions") {
    const CloseUpData c = parse_closeup(kCloseUp);

    CHECK(c.id == "skull");
    CHECK(c.background == "closeups/skull.png");
    REQUIRE(c.hotspots.size() == 2);

    // hotspot_at returns the polygon under the point; the look hotspot has no goto.
    const CloseUpHotspot* jaw = c.hotspot_at({50.0f, 50.0f});
    REQUIRE(jaw != nullptr);
    CHECK(jaw->name == "la mandíbula");
    CHECK(jaw->goto_scene.empty());

    // The other hotspot carries a scene-transition action.
    const CloseUpHotspot* panel = c.hotspot_at({250.0f, 250.0f});
    REQUIRE(panel != nullptr);
    CHECK(panel->goto_scene == "hall_map");

    // A miss outside every polygon.
    CHECK(c.hotspot_at({500.0f, 500.0f}) == nullptr);
}

TEST_CASE("parse_closeup resolves background relative to the close-up YAML path") {
    const char* yaml = "id: lab_chalkboard\nbackground: background.png\n";
    // Co-located: relative to the YAML's directory.
    CHECK(parse_closeup(yaml, {}, "closeups/lab_chalkboard/closeup.yml").background ==
          "closeups/lab_chalkboard/background.png");
    // Leading '/' escapes to a resources-root-relative path (shared asset).
    CHECK(parse_closeup("id: x\nbackground: /rooms/b/bg.png\n", {}, "closeups/x/closeup.yml")
              .background == "rooms/b/bg.png");
    // No logical_path -> verbatim (headless tests).
    CHECK(parse_closeup(yaml).background == "background.png");
    // A path that can't resolve to a valid logical path is rejected.
    CHECK(error_code([] {
              parse_closeup("id: x\nbackground: ../escape.png\n", {}, "closeups/x/closeup.yml");
          }) == "closeup.background-path-invalid");
}

TEST_CASE("scene id and game close-up color can supply authored defaults") {
    const CloseUpData closeup = parse_closeup("background: background.png\n",
                                              "letter",
                                              "chapters/one/closeups/letter/closeup.yml",
                                              sf::Color(20, 16, 12));
    CHECK(closeup.id == "letter");
    CHECK(closeup.background == "chapters/one/closeups/letter/background.png");
    CHECK(closeup.background_color == sf::Color(20, 16, 12));
}

TEST_CASE("parse_closeup enforces id/background and hotspot geometry with stable codes") {
    CHECK(error_code([] { parse_closeup("background: a.png\n"); }) == "closeup.id-missing");
    CHECK(error_code([] { parse_closeup("id: x\n"); }) == "closeup.background-missing");
    CHECK(error_code([] { parse_closeup(kCloseUp, "other"); }) == "closeup.id-mismatch");
    CHECK(error_code([] {
              parse_closeup("id: x\nbackground: a.png\nhotspots:\n  h: { name: n }\n");
          }) == "closeup.hotspot-no-area");
    CHECK(error_code([] {
              parse_closeup(
                  "id: x\nbackground: a.png\nhotspots:\n  h:\n    area: [ {x: 0, y: 0} ]\n");
          }) == "closeup.hotspot-area-degenerate");
}
