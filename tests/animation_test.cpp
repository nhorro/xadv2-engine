#include "engine/gfx/animation.hpp"
#include "engine/gfx/asset_error.hpp"
#include "engine/gfx/sequence_player.hpp"

#include <doctest/doctest.h>

using namespace pac::gfx;

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
)YAML";

} // namespace

TEST_CASE("parse_animation reads pivot, spritesheet, and sequences") {
    const Animation a = parse_animation(kAnim);
    CHECK(a.spritesheet == "atlas.yml");
    CHECK(a.pivot == "foot");
    REQUIRE(a.has("walk"));
    CHECK(a.sequence("walk")->frames.size() == 3);
    CHECK(a.sequence("walk")->loop);
    CHECK_FALSE(a.sequence("wave")->loop);
    CHECK(a.sequence("nope") == nullptr);
}

TEST_CASE("parse_animation rejects missing sequences") {
    CHECK_THROWS_AS(parse_animation("pivot: foot\n"), AssetError);
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
