#include "engine/gfx/asset_error.hpp"
#include "engine/gfx/composite_sprite.hpp"

#include <doctest/doctest.h>
#include <SFML/Graphics/Texture.hpp>

using namespace pac::gfx;

namespace {

AnimatedSprite part_sprite(const sf::Texture& texture,
                           sf::IntRect rect,
                           std::map<std::string, sf::Vector2f> anchors,
                           const std::string& pivot) {
    SpritesheetData sheet;
    Frame frame;
    frame.rect = rect;
    frame.anchors = std::move(anchors);
    sheet.frames.emplace("frame", std::move(frame));
    Animation animation;
    animation.pivot = pivot;
    Sequence idle;
    idle.loop = true;
    idle.frames.push_back({"frame", 1.0f});
    animation.sequences.emplace("idle", std::move(idle));
    return AnimatedSprite(Spritesheet(std::move(sheet), texture), std::move(animation));
}

} // namespace

TEST_CASE("composite parser topologically orders attachments and reads rotation tracks") {
    const CompositeDefinition definition = parse_composite(R"YAML(
version: 1
root: { id: body, animation: body.anim.yml, z: 2 }
children:
  - id: wheel
    parent: body
    animation: wheel.anim.yml
    parent_anchor: mount
    child_anchor: center
    z: 1
sequences:
  moving:
    parts:
      body: { sequence: idle }
      wheel:
        sequence: idle
        rotation: { from: 0, to: 360, duration: 1.0, loop: true }
)YAML");

    REQUIRE(definition.nodes.size() == 2);
    CHECK(definition.nodes[0].id == "body");
    CHECK(definition.nodes[1].parent == "body");
    const auto& wheel = definition.sequences.at("moving").parts.at("wheel");
    REQUIRE(wheel.rotation.has_value());
    CHECK(wheel.rotation->to == doctest::Approx(360.0f));
    CHECK(wheel.rotation->loop);
}

TEST_CASE("composite parser rejects unknown parents and attachment cycles") {
    CHECK_THROWS_AS(parse_composite(R"YAML(
root: { id: body, animation: body.anim.yml }
children:
  - { id: wheel, parent: ghost, animation: wheel.anim.yml,
      parent_anchor: mount, child_anchor: center }
sequences: { idle: { parts: { body: { sequence: idle } } } }
)YAML"),
                    AssetError);

    CHECK_THROWS_AS(parse_composite(R"YAML(
root: { id: body, animation: body.anim.yml }
children:
  - { id: a, parent: b, animation: a.anim.yml,
      parent_anchor: mount, child_anchor: center }
  - { id: b, parent: a, animation: b.anim.yml,
      parent_anchor: mount, child_anchor: center }
sequences: { idle: { parts: { body: { sequence: idle } } } }
)YAML"),
                    AssetError);
}

TEST_CASE("composite child stays attached while its local rotation advances") {
    sf::Texture texture;
    CompositeDefinition definition;
    CompositeNodeDefinition body;
    body.id = "body";
    body.animation = "body.anim.yml";
    definition.nodes.push_back(body);
    CompositeNodeDefinition wheel;
    wheel.id = "wheel";
    wheel.parent = "body";
    wheel.animation = "wheel.anim.yml";
    wheel.parent_anchor = "mount";
    wheel.child_anchor = "center";
    wheel.z = -1;
    definition.nodes.push_back(wheel);

    CompositeSequence moving;
    moving.parts["body"].sequence = "idle";
    moving.parts["wheel"].sequence = "idle";
    moving.parts["wheel"].rotation = CompositeRotationTrack{0.0f, 360.0f, 1.0f, true};
    definition.sequences.emplace("moving", std::move(moving));

    std::vector<AnimatedSprite> sprites;
    sprites.push_back(part_sprite(texture,
                                  {0, 0, 100, 50},
                                  {{"pivot", {0.0f, 0.0f}}, {"mount", {10.0f, 20.0f}}},
                                  "pivot"));
    sprites.push_back(part_sprite(texture,
                                  {0, 0, 10, 10},
                                  {{"center", {5.0f, 5.0f}}, {"marker", {10.0f, 5.0f}}},
                                  "center"));

    CompositeSprite composite(std::move(definition), std::move(sprites));
    composite.setPosition(100.0f, 200.0f);
    composite.play("moving");
    REQUIRE(composite.anchor_world("wheel.marker").has_value());
    CHECK(composite.anchor_world("wheel.marker")->x == doctest::Approx(115.0f));
    CHECK(composite.anchor_world("wheel.marker")->y == doctest::Approx(220.0f));

    composite.update(0.25f); // quarter turn: marker moves from right to below center
    REQUIRE(composite.anchor_world("wheel.marker").has_value());
    CHECK(composite.anchor_world("wheel.marker")->x == doctest::Approx(110.0f));
    CHECK(composite.anchor_world("wheel.marker")->y == doctest::Approx(225.0f));
    CHECK_FALSE(composite.finished()); // looping track/part remains active
}
