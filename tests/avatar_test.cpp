#include "engine/gfx/animated_sprite.hpp"
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/room.hpp"

#include <doctest/doctest.h>
#include <SFML/Graphics/Texture.hpp>

#include <initializer_list>
#include <string>
#include <utility>

using namespace pac;

namespace {

gfx::Animation animation(std::initializer_list<std::string> names) {
    gfx::Animation anim;
    for (const std::string& name : names) {
        gfx::Sequence sequence;
        sequence.loop = true;
        sequence.frames.push_back({"frame", 0.1f});
        anim.sequences.emplace(name, std::move(sequence));
    }
    return anim;
}

gfx::Spritesheet sheet(const sf::Texture& texture) {
    gfx::SpritesheetData data;
    data.frames.emplace("frame", gfx::Frame{});
    return gfx::Spritesheet(std::move(data), texture);
}

} // namespace

TEST_CASE("Avatar talk prefers its facing and falls back to an available direction") {
    sf::Texture texture;
    pnc::Avatar avatar(
        gfx::AnimatedSprite(sheet(texture),
                            animation({"stand_down", "stand_left", "talk_left", "talk_right"})));

    avatar.face("left");
    avatar.talk();
    CHECK(avatar.talking());
    CHECK(avatar.current_animation() == "talk_left");

    avatar.stop_talking();
    avatar.face("down");
    avatar.talk();
    CHECK(avatar.talking());
    CHECK(avatar.current_animation() == "talk_right");
}

TEST_CASE("Avatar talk accepts a direction-neutral fallback") {
    sf::Texture texture;
    pnc::Avatar avatar(gfx::AnimatedSprite(sheet(texture), animation({"stand_down", "talk"})));

    avatar.talk();
    CHECK(avatar.talking());
    CHECK(avatar.current_animation() == "talk");
}

TEST_CASE("Avatar talk stops movement and restores standing when speech ends") {
    sf::Texture texture;
    pnc::Avatar avatar(
        gfx::AnimatedSprite(sheet(texture),
                            animation({"stand_down", "stand_right", "walk_right", "talk_right"})));
    avatar.set_position({0.0f, 0.0f});
    avatar.move_to({100.0f, 0.0f});
    REQUIRE(avatar.moving());
    REQUIRE(avatar.current_animation() == "walk_right");

    avatar.talk();
    CHECK_FALSE(avatar.moving());
    CHECK(avatar.current_animation() == "talk_right");

    avatar.stop_talking();
    CHECK_FALSE(avatar.talking());
    CHECK(avatar.current_animation() == "stand_right");
}

TEST_CASE("Avatar without a talk rig safely remains standing") {
    sf::Texture texture;
    pnc::Avatar avatar(gfx::AnimatedSprite(sheet(texture), animation({"stand_down"})));

    avatar.talk();
    CHECK_FALSE(avatar.talking());
    CHECK(avatar.current_animation() == "stand_down");
}

TEST_CASE("Avatar shadow opacity clamps, transitions, and can be interrupted") {
    sf::Texture texture;
    pnc::Avatar avatar(gfx::AnimatedSprite(sheet(texture), animation({"stand_down"})));
    pnc::RoomData room;

    CHECK(avatar.shadow_opacity() == doctest::Approx(1.0f));

    avatar.set_shadow_opacity(-1.0f);
    CHECK(avatar.shadow_opacity() == doctest::Approx(0.0f));

    avatar.set_shadow_opacity(1.0f, 2.0f);
    avatar.update(0.5f, room);
    CHECK(avatar.shadow_opacity() == doctest::Approx(0.25f));
    avatar.update(0.5f, room);
    CHECK(avatar.shadow_opacity() == doctest::Approx(0.5f));

    // A new request starts at the live interpolated value, not the old endpoint.
    avatar.set_shadow_opacity(0.2f, 1.0f);
    avatar.update(0.5f, room);
    CHECK(avatar.shadow_opacity() == doctest::Approx(0.35f));
    avatar.update(0.5f, room);
    CHECK(avatar.shadow_opacity() == doctest::Approx(0.2f));

    avatar.set_shadow_opacity(2.0f);
    CHECK(avatar.shadow_opacity() == doctest::Approx(1.0f));
}
