#include "engine/gfx/asset_error.hpp"
#include "engine/gfx/script_scene.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

using namespace pac::gfx;
using pac::test::error_code;

TEST_CASE("parse_script_scene reads component-shaped entities") {
    const ScriptSceneData scene = parse_script_scene(R"yaml(
version: 1
id: sandbox
opaque: false
background:
  color: { r: 12, g: 34, b: 56, a: 78 }
  image: /backgrounds/lab.png
entities:
  cursor:
    sprite:
      image: cursor.png
      origin: { x: 8, y: 9 }
      tint: { r: 200, g: 201, b: 202 }
    transform:
      position: { x: 100, y: 120 }
      scale: { x: 2, y: 3 }
      rotation: 15
    z: 9
    visible: false
  hero:
    animation:
      source: /characters/hero/hero.anim.yml
      sequence: stand_down
)yaml",
                                                     "sandbox",
                                                     "scenes/sandbox/scene.yaml");

    CHECK(scene.version == 1);
    CHECK(scene.id == "sandbox");
    CHECK_FALSE(scene.opaque);
    CHECK(scene.background_color == sf::Color(12, 34, 56, 78));
    CHECK(scene.background_image == "backgrounds/lab.png");
    REQUIRE(scene.entities.size() == 2);

    const ScriptSceneEntityData& cursor = scene.entities[0];
    CHECK(cursor.id == "cursor");
    REQUIRE(cursor.sprite.has_value());
    CHECK(cursor.sprite->image == "scenes/sandbox/cursor.png");
    CHECK(cursor.sprite->origin == sf::Vector2f(8.0f, 9.0f));
    CHECK(cursor.sprite->tint == sf::Color(200, 201, 202));
    CHECK(cursor.transform.position == sf::Vector2f(100.0f, 120.0f));
    CHECK(cursor.transform.scale == sf::Vector2f(2.0f, 3.0f));
    CHECK(cursor.transform.rotation == doctest::Approx(15.0f));
    CHECK(cursor.z == doctest::Approx(9.0f));
    CHECK_FALSE(cursor.visible);

    const ScriptSceneEntityData& hero = scene.entities[1];
    REQUIRE(hero.animation.has_value());
    CHECK(hero.animation->source == "characters/hero/hero.anim.yml");
    CHECK(hero.animation->sequence == "stand_down");
}

TEST_CASE("parse_script_scene permits an empty entity registry") {
    const ScriptSceneData scene =
        parse_script_scene("id: empty\nbackground: { color: { r: 1, g: 2, b: 3 } }\n",
                           "empty",
                           "scenes/empty/scene.yaml");
    CHECK(scene.entities.empty());
    CHECK(scene.background_color == sf::Color(1, 2, 3));
}

TEST_CASE("parse_script_scene reports stable structural diagnostics") {
    CHECK(error_code([] { parse_script_scene("id: actual\n", "expected"); }) ==
          "script-scene.id-mismatch");
    CHECK(error_code([] { parse_script_scene("id: x\nentities: []\n"); }) ==
          "script-scene.entities-not-map");
    CHECK(error_code([] {
              parse_script_scene("id: x\nentities:\n  orphan:\n    visible: true\n");
          }) == "script-scene.visual-count-invalid");
    CHECK(error_code([] {
              parse_script_scene("id: x\nentities:\n  both:\n    sprite: { image: a.png }\n"
                                 "    animation: { source: a.yml, sequence: idle }\n");
          }) == "script-scene.visual-count-invalid");
    CHECK(error_code([] {
              parse_script_scene("id: x\nentities:\n  bad:\n    sprite: { image: ../escape.png }\n",
                                 {},
                                 "scene.yaml");
          }) == "script-scene.asset-path-invalid");
}
