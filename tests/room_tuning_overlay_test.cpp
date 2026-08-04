#include "engine/pnc/room.hpp"
#include "pnc/room_tuning_overlay.hpp"

#include <doctest/doctest.h>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <array>
#include <string>

using namespace pac::pnc;

namespace {

RoomData tuning_room() {
    return parse_room(R"yaml(
id: test
post_process:
  shader:
    source: shaders/grade.frag
    params:
      tint: [1.0, 0.9, 0.8]
      brightness: -0.05
      contrast: 1.1
lighting:
  ambient: {color: [0.5, 0.6, 0.7], intensity: 0.4}
  normal_map: {image: normals.png, strength: 0.8}
  lights:
    - id: lamp
      type: omni
      at: {x: 100, y: 120}
      radius: 240
      color: [1.0, 0.8, 0.5]
      intensity: 0.9
      modulation: {type: flicker, amount: 0.1, speed: 4, seed: 3}
  occluders:
    - id: wall
      area: [{x: 10, y: 20}, {x: 30, y: 40}]
  projected_shadows:
    source: lamp
    opacity: 0.2
)yaml");
}

void click(RoomTuningOverlay& overlay, int x, int y) {
    sf::Event event{};
    event.type = sf::Event::MouseButtonPressed;
    event.mouseButton.button = sf::Mouse::Left;
    event.mouseButton.x = x;
    event.mouseButton.y = y;
    CHECK(overlay.handle_event(event));
    event.type = sf::Event::MouseButtonReleased;
    CHECK(overlay.handle_event(event));
}

} // namespace

TEST_CASE("room tuning overlay uses working render values and can compare/reset") {
    const RoomData room = tuning_room();
    RoomTuningOverlay overlay;
    overlay.open(room, {0.0f, 612.0f, 1280.0f, 108.0f}, nullptr);

    CHECK(overlay.active());
    REQUIRE(overlay.effective_lighting(room) != nullptr);
    CHECK(overlay.effective_lighting(room)->ambient_intensity == doctest::Approx(0.4f));

    overlay.working_lighting().ambient_intensity = 0.72f;
    CHECK(overlay.effective_lighting(room)->ambient_intensity == doctest::Approx(0.72f));

    // Reset restores the parsed room values without touching RoomData itself.
    overlay.reset();
    CHECK(overlay.effective_lighting(room)->ambient_intensity == doctest::Approx(0.4f));
    CHECK(room.dynamic_lighting->ambient_intensity == doctest::Approx(0.4f));
}

TEST_CASE("room tuning YAML round-trips adjusted lighting and grading") {
    const RoomData room = tuning_room();
    RoomTuningOverlay overlay;
    overlay.open(room, {0.0f, 612.0f, 1280.0f, 108.0f}, nullptr);
    overlay.working_lighting().ambient_intensity = 0.63f;
    overlay.working_lighting().lights[0].radius = 315.0f;
    overlay.working_lighting().lights[0].modulation.speed = 7.5f;

    REQUIRE(overlay.working_post_process());
    auto& brightness = overlay.working_post_process()->shaders[0].params[1].value;
    REQUIRE(std::holds_alternative<float>(brightness));
    brightness = 0.12f;

    const RoomData exported = parse_room("id: adjusted\n" + overlay.yaml());
    REQUIRE(exported.dynamic_lighting);
    CHECK(exported.dynamic_lighting->ambient_intensity == doctest::Approx(0.63f));
    REQUIRE(exported.dynamic_lighting->lights.size() == 1);
    CHECK(exported.dynamic_lighting->lights[0].radius == doctest::Approx(315.0f));
    CHECK(exported.dynamic_lighting->lights[0].modulation.speed == doctest::Approx(7.5f));
    REQUIRE(exported.dynamic_lighting->occluders.size() == 1);
    REQUIRE(exported.projected_shadow);
    CHECK(exported.projected_shadow->source == "lamp");

    REQUIRE(exported.post_process);
    REQUIRE(exported.post_process->shaders[0].params.size() == 3);
    CHECK(std::get<float>(exported.post_process->shaders[0].params[1].value) ==
          doctest::Approx(0.12f));
}

TEST_CASE("unlit room tuning can preview and export a default ambient pass") {
    RoomData room;
    room.id = "plain";
    RoomTuningOverlay overlay;
    overlay.open(room, {0.0f, 612.0f, 1280.0f, 108.0f}, nullptr);

    REQUIRE(overlay.effective_lighting(room) != nullptr);
    CHECK(overlay.effective_lighting(room)->ambient_intensity == doctest::Approx(0.35f));
    CHECK(overlay.yaml().find("lighting:") != std::string::npos);
    CHECK(overlay.yaml().find("post_process:") == std::string::npos);
}

TEST_CASE("room tuning controls consume input and adjust live values") {
    const RoomData room = tuning_room();
    RoomTuningOverlay overlay;
    overlay.open(room, {0.0f, 612.0f, 1280.0f, 108.0f}, nullptr);

    // Lights tab, then the center of its intensity slider (0..4 -> 2).
    click(overlay, 145, 628);
    click(overlay, 380, 682);
    CHECK(overlay.working_lighting().lights[0].intensity == doctest::Approx(2.0f).epsilon(0.05));

    // The compare button switches effective rendering back to authored values.
    click(overlay, 1020, 628);
    CHECK(overlay.compare_original());
    CHECK(overlay.effective_lighting(room)->lights[0].intensity == doctest::Approx(0.9f));

    // Any otherwise unrelated event is still consumed while the overlay owns input.
    sf::Event event{};
    event.type = sf::Event::KeyPressed;
    event.key.code = sf::Keyboard::A;
    CHECK(overlay.handle_event(event));
    CHECK(overlay.active());

    event.key.code = sf::Keyboard::Escape;
    CHECK(overlay.handle_event(event));
    CHECK_FALSE(overlay.active());
}

TEST_CASE("grading controls edit generic shader parameters") {
    const RoomData room = tuning_room();
    RoomTuningOverlay overlay;
    overlay.open(room, {0.0f, 612.0f, 1280.0f, 108.0f}, nullptr);

    click(overlay, 240, 628); // Grading tab.
    click(overlay, 260, 682); // Next param: brightness.
    click(overlay, 790, 682); // 50% of -1..1, approximately zero.

    REQUIRE(overlay.working_post_process());
    const auto& brightness = overlay.working_post_process()->shaders[0].params[1].value;
    REQUIRE(std::holds_alternative<float>(brightness));
    CHECK(std::get<float>(brightness) == doctest::Approx(0.0f).epsilon(0.03));
}
