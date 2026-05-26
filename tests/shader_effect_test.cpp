// Shader configuration parsing (design 03/06 §Shaders). The runtime application
// of shaders needs a GL context and is exercised by the sample, not here; this
// covers the headless data: YAML -> gfx::ShaderEffect / ShaderParam off parse_room.

#include "engine/core/resource_cache.hpp"
#include "engine/gfx/shader_effect.hpp"
#include "engine/pnc/data_error.hpp"
#include "engine/pnc/room.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <variant>

using namespace pac::pnc;
using pac::gfx::ShaderEffect;
using pac::gfx::ShaderParam;
using pac::test::error_code;

namespace {

// Find a param by name in an effect, or fail the test.
const ShaderParam& param(const ShaderEffect& fx, const std::string& name) {
    for (const ShaderParam& p : fx.params) {
        if (p.name == name) {
            return p;
        }
    }
    FAIL("missing shader param: ", name);
    return fx.params.front(); // unreachable
}

} // namespace

TEST_CASE("layer shader: shorthand string form") {
    const RoomData r = parse_room(R"YAML(
id: r
background:
  layers:
    - { id: bg, image: a/bg.png, shader: shaders/wave.frag }
)YAML");
    REQUIRE(r.layers.size() == 1);
    REQUIRE(r.layers[0].shaders.size() == 1);
    const ShaderEffect& fx = r.layers[0].shaders[0];
    CHECK(fx.source == "shaders/wave.frag");
    CHECK(fx.enabled);
    CHECK(fx.controller.empty());
    CHECK(fx.params.empty());
}

TEST_CASE("layer shader: full form infers param types") {
    const RoomData r = parse_room(R"YAML(
id: r
background:
  layers:
    - id: bg
      image: a/bg.png
      shader:
        source: shaders/water.frag
        enabled: false
        controller: custom_water
        params:
          wind_speed: 0.5
          on: true
          samples: { type: int, value: 8 }
          center: [0.5, 0.3]
          tint: [0.2, 0.4, 0.8, 1.0]
)YAML");
    REQUIRE(r.layers[0].shaders.size() == 1);
    const ShaderEffect& fx = r.layers[0].shaders[0];
    CHECK(fx.source == "shaders/water.frag");
    CHECK_FALSE(fx.enabled);
    CHECK(fx.controller == "custom_water");

    CHECK(std::get<float>(param(fx, "wind_speed").value) == doctest::Approx(0.5f));
    CHECK(std::get<bool>(param(fx, "on").value) == true);
    CHECK(std::get<int>(param(fx, "samples").value) == 8); // explicit int, not float

    const auto& center = std::get<std::array<float, 2>>(param(fx, "center").value);
    CHECK(center[0] == doctest::Approx(0.5f));
    CHECK(center[1] == doctest::Approx(0.3f));

    const auto& tint = std::get<std::array<float, 4>>(param(fx, "tint").value);
    CHECK(tint[3] == doctest::Approx(1.0f));
}

TEST_CASE("bare integer scalar infers as float (declare {type: int} for an int)") {
    const RoomData r = parse_room(R"YAML(
id: r
background:
  layers:
    - id: bg
      image: a/bg.png
      shader:
        source: s.frag
        params: { count: 4 }
)YAML");
    const ShaderParam& p = r.layers[0].shaders[0].params.front();
    CHECK(std::holds_alternative<float>(p.value));
    CHECK(std::get<float>(p.value) == doctest::Approx(4.0f));
}

TEST_CASE("shaders list keeps order (multi-pass chain feeds them in order)") {
    const RoomData r = parse_room(R"YAML(
id: r
background:
  layers:
    - id: bg
      image: a/bg.png
      shaders:
        - shaders/a.frag
        - { source: shaders/b.frag }
)YAML");
    REQUIRE(r.layers[0].shaders.size() == 2);
    CHECK(r.layers[0].shaders[0].source == "shaders/a.frag");
    CHECK(r.layers[0].shaders[1].source == "shaders/b.frag");
}

TEST_CASE("regions and objects accept shaders too") {
    const RoomData r = parse_room(R"YAML(
id: r
regions:
  panel:
    area: [ {x: 0, y: 0}, {x: 1, y: 0}, {x: 1, y: 1} ]
    states: { on: a/on.png }
    shader: shaders/glow.frag
objects:
  crt:
    image: a/crt.png
    shader: { source: shaders/crt.frag, params: { curvature: 0.2 } }
)YAML");
    REQUIRE(r.regions.at("panel").shaders.size() == 1);
    CHECK(r.regions.at("panel").shaders[0].source == "shaders/glow.frag");
    REQUIRE(r.objects.at("crt").shaders.size() == 1);
    CHECK(r.objects.at("crt").shaders[0].source == "shaders/crt.frag");
    CHECK(std::get<float>(param(r.objects.at("crt").shaders[0], "curvature").value) ==
          doctest::Approx(0.2f));
}

TEST_CASE("shader_source_uses matches whole words only") {
    using pac::core::shader_source_uses;

    // The reserved `texture` sampler is declared and used; `texture2D` must not
    // be mistaken for it (that false positive would set an absent uniform).
    const std::string grade =
        "uniform sampler2D texture;\nuniform vec3 tint;\n"
        "void main(){ gl_FragColor = texture2D(texture, gl_TexCoord[0].xy) * tint; }";
    CHECK(shader_source_uses(grade, "texture"));            // the declared sampler
    CHECK(shader_source_uses(grade, "tint"));               // an author param
    CHECK_FALSE(shader_source_uses(grade, "u_time"));       // not declared
    CHECK_FALSE(shader_source_uses(grade, "u_resolution")); // not declared

    // A time-driven shader does declare the reserved time uniform.
    const std::string flicker =
        "uniform float u_time;\nvoid main(){ gl_FragColor = vec4(u_time); }";
    CHECK(shader_source_uses(flicker, "u_time"));
    CHECK_FALSE(shader_source_uses(flicker, "u_resolution"));

    // A substring that is part of a longer identifier is not a match.
    CHECK_FALSE(shader_source_uses("uniform float u_time_scale;", "u_time"));
}

TEST_CASE("shader parse errors carry stable codes") {
    CHECK(error_code([] {
              parse_room("id: r\nbackground:\n  layers:\n"
                         "    - { id: bg, image: a.png, shader: { params: { x: 1 } } }\n");
          }) == "room.shader-source-missing");

    CHECK(error_code([] {
              parse_room("id: r\nbackground:\n  layers:\n"
                         "    - { id: bg, image: a.png, shader: { source: s.frag, "
                         "params: { c: [1, 2, 3, 4, 5] } } }\n");
          }) == "room.shader-param-vec-size");

    CHECK(error_code([] {
              parse_room("id: r\nbackground:\n  layers:\n"
                         "    - { id: bg, image: a.png, shader: { source: s.frag, "
                         "params: { c: { type: mat4, value: 1 } } } }\n");
          }) == "room.shader-param-type");
}
