#include "engine/gfx/animated_sprite.hpp"
#include "engine/gfx/animation.hpp"
#include "engine/gfx/asset_error.hpp"
#include "engine/gfx/sequence_player.hpp"
#include "engine/gfx/spritesheet.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>
#include <SFML/Graphics/Texture.hpp>

using namespace pac::gfx;
using pac::test::error_code;

namespace {

const char* kAnim = R"YAML(
spritesheet: atlas.yml
pivot: foot
sequences:
  idle:
    loop: true
    frames:
      - { sprite: f0, duration: 0.10 }
  walk:
    loop: true
    frames:
      - { sprite: f0, duration: 0.10 }
      - { sprite: f1, duration: 0.10 }
      - { sprite: f2, duration: 0.10 }
  wave:
    loop: false
    frames:
      - { sprite: w0, duration: 0.10 }
      - { sprite: w1, duration: 0.10 }
  walk_left:
    loop: true
    h_mirror: true
    frames:
      - { sprite: f0, duration: 0.10 }
)YAML";

} // namespace

TEST_CASE("parse_animation reads pivot, spritesheet, and sequences") {
    const Animation a = parse_animation(kAnim);
    CHECK(a.spritesheet == "atlas.yml");
    CHECK(a.pivot == "foot");
    REQUIRE(a.has("walk"));
    CHECK(a.sequence("walk")->frames.size() == 3);
    CHECK(a.sequence("walk")->loop);
    CHECK_FALSE(a.sequence("walk")->h_mirror);
    CHECK_FALSE(a.sequence("wave")->loop);
    CHECK(a.sequence("walk_left")->h_mirror);
    CHECK(a.sequence("nope") == nullptr);
}

TEST_CASE("parse_animation rejects missing sequences") {
    CHECK_THROWS_AS(parse_animation("pivot: foot\n"), AssetError);
    CHECK(error_code([] { parse_animation("pivot: foot\n"); }) == "anim.sequences-not-map");
    CHECK(error_code([] {
              parse_animation("sequences:\n  idle:\n    frames:\n      - { duration: 0.1 }\n");
          }) == "anim.frame-sprite-missing");
}

TEST_CASE("SequencePlayer advances frames and loops") {
    SequencePlayer p(parse_animation(kAnim));
    p.play("walk");
    CHECK(p.current_frame_id() == "f0");
    p.update(0.10f);
    CHECK(p.current_frame_id() == "f1");
    p.update(0.10f);
    CHECK(p.current_frame_id() == "f2");
    p.update(0.10f); // wraps
    CHECK(p.current_frame_id() == "f0");
    CHECK_FALSE(p.finished());
}

TEST_CASE("SequencePlayer handles a large dt across multiple frames") {
    SequencePlayer p(parse_animation(kAnim));
    p.play("walk");
    p.update(0.25f); // 2.5 frame durations
    CHECK(p.current_frame_id() == "f2");
}

TEST_CASE("SequencePlayer exposes horizontal mirroring for the current sequence") {
    SequencePlayer p(parse_animation(kAnim));

    CHECK_FALSE(p.current_h_mirror());
    p.play("walk");
    CHECK_FALSE(p.current_h_mirror());
    p.play("walk_left");
    CHECK(p.current_h_mirror());
}

TEST_CASE("non-looping sequence finishes, fires callback, and holds last frame") {
    SequencePlayer p(parse_animation(kAnim));
    bool done = false;
    p.set_on_finished([&done]() { done = true; });
    p.play("wave");
    CHECK(p.current_frame_id() == "w0");
    p.update(0.10f);
    CHECK(p.current_frame_id() == "w1");
    CHECK_FALSE(p.finished());
    p.update(0.10f); // past the last frame
    CHECK(p.finished());
    CHECK(done);
    CHECK(p.current_frame_id() == "w1"); // holds last frame
    p.update(1.0f);                      // no further movement
    CHECK(p.current_frame_id() == "w1");
}

TEST_CASE("AnimatedSprite mirrors bounds and frame-local anchors around its pivot") {
    sf::Texture texture;
    SpritesheetData sheet_data;
    Frame frame;
    frame.rect = sf::IntRect(0, 0, 10, 20);
    frame.anchors["foot"] = {2.0f, 18.0f};
    frame.anchors["hand"] = {8.0f, 5.0f};
    sheet_data.frames.emplace("frame", std::move(frame));

    Animation anim;
    anim.pivot = "foot";
    Sequence normal;
    normal.frames.push_back({"frame", 0.1f});
    anim.sequences.emplace("normal", normal);
    Sequence mirrored = normal;
    mirrored.h_mirror = true;
    anim.sequences.emplace("mirrored", mirrored);

    AnimatedSprite sprite(Spritesheet(std::move(sheet_data), texture), std::move(anim));
    sprite.setPosition(100.0f, 200.0f);

    sprite.play("normal");
    sf::FloatRect bounds = sprite.global_bounds();
    CHECK(bounds.left == doctest::Approx(98.0f));
    CHECK(bounds.top == doctest::Approx(182.0f));
    CHECK(bounds.width == doctest::Approx(10.0f));
    CHECK(bounds.height == doctest::Approx(20.0f));
    REQUIRE(sprite.anchor_world("hand").has_value());
    CHECK(sprite.anchor_world("hand")->x == doctest::Approx(106.0f));
    CHECK(sprite.anchor_world("hand")->y == doctest::Approx(187.0f));

    sprite.play("mirrored");
    bounds = sprite.global_bounds();
    CHECK(bounds.left == doctest::Approx(92.0f));
    CHECK(bounds.top == doctest::Approx(182.0f));
    CHECK(bounds.width == doctest::Approx(10.0f));
    CHECK(bounds.height == doctest::Approx(20.0f));
    REQUIRE(sprite.anchor_world("hand").has_value());
    CHECK(sprite.anchor_world("hand")->x == doctest::Approx(94.0f));
    CHECK(sprite.anchor_world("hand")->y == doctest::Approx(187.0f));
}
