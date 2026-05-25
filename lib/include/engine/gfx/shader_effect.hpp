#pragma once

#include <array>
#include <string>
#include <variant>
#include <vector>

namespace sf {
class Shader;
}

/// Generic 2D shader configuration (design 03 §Shaders). These are pure data,
/// parsed from YAML by the loaders that own a drawable (e.g. room layers in
/// `pac::pnc`), and headless-testable: only `apply_shader_params` touches SFML.
namespace pac::gfx {

/// A shader uniform value. The variant covers the primitive GLSL types the engine
/// binds generically, so most shaders need no C++:
///   bool        -> uniform bool
///   int         -> uniform int
///   float       -> uniform float
///   array<2..4> -> uniform vec2 / vec3 / vec4
using ShaderValue = std::
    variant<bool, int, float, std::array<float, 2>, std::array<float, 3>, std::array<float, 4>>;

/// One named uniform fed to a shader from YAML.
struct ShaderParam {
    std::string name;
    ShaderValue value;
};

/// A shader applied to a drawable: the fragment-source logical path, its
/// declarative uniform params, and an optional `controller` id. `source` is
/// resolved relative to the resources root (shaders are shared across rooms),
/// unlike room-relative image paths. `controller` names a C++ hook for uniforms
/// the declarative path can't express (design-for; not yet implemented).
struct ShaderEffect {
    std::string source;
    std::vector<ShaderParam> params;
    std::string controller; // design-for escape hatch; see design 03 §Shaders
    bool enabled = true;
};

/// Set each param as a uniform on `shader` (no-op for a uniform the program does
/// not declare — SFML tolerates that). Reserved uniforms (`u_time`,
/// `u_resolution`, `texture`) are set by the renderer, not here.
void apply_shader_params(sf::Shader& shader, const std::vector<ShaderParam>& params);

} // namespace pac::gfx
