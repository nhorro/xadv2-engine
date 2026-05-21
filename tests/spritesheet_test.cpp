#include "engine/gfx/asset_error.hpp"
#include "engine/gfx/spritesheet.hpp"

#include <doctest/doctest.h>

using namespace pac::gfx;

namespace {

const char* kSheet = R"YAML(
image: "body.png"
size: { width: 256, height: 128 }
sprites:
  - id: a
    rect: { x: 0, y: 0, width: 32, height: 64 }
    anchors:
      foot: { x: 16, y: 64 }
  - id: b
    rect: { x: 32, y: 0, width: 30, height: 64 }
)YAML";

} // namespace

TEST_CASE("parse_spritesheet reads image, size, frames, and anchors") {
    const SpritesheetData d = parse_spritesheet(kSheet);
    CHECK(d.image == "body.png");
    CHECK(d.size.x == 256u);
    CHECK(d.size.y == 128u);
    CHECK(d.frame_count() == 2);

    REQUIRE(d.has("a"));
    const Frame* a = d.frame("a");
    REQUIRE(a != nullptr);
    CHECK(a->rect.left == 0);
    CHECK(a->rect.top == 0);
    CHECK(a->rect.width == 32);
    CHECK(a->rect.height == 64);

    const sf::Vector2f* foot = a->anchor("foot");
    REQUIRE(foot != nullptr);
    CHECK(foot->x == doctest::Approx(16.0f));
    CHECK(foot->y == doctest::Approx(64.0f));
    CHECK(a->anchor("missing") == nullptr);

    const Frame* b = d.frame("b");
    REQUIRE(b != nullptr);
    CHECK(b->rect.width == 30);
    CHECK(b->anchor("foot") == nullptr); // b declares no anchors

    CHECK(d.frame("nope") == nullptr);
}

TEST_CASE("parse_spritesheet rejects malformed atlases") {
    CHECK_THROWS_AS(parse_spritesheet("image: x.png\n"), AssetError); // no sprites
    CHECK_THROWS_AS(parse_spritesheet("sprites:\n  - rect: { x: 0, y: 0, width: 1, height: 1 }\n"),
                    AssetError); // sprite without id
}
