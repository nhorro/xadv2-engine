#pragma once

#include <string>

namespace sf {
class RenderTarget;
}

namespace pac::core {
class Diagnostics;
}

namespace pac::gfx {

/// Installs the GLES2 program used for ordinary SFML sprites, shapes, and text.
/// Desktop SFML already supplies its fixed-function compatibility path, so this
/// is a no-op there. Must be called after the first window/context is created.
bool initialize_gles2_renderer(sf::RenderTarget& target, pac::core::Diagnostics& log);

/// Assigns the process-wide default GLES2 shader to a newly-created render
/// target. No-op on desktop. initialize_gles2_renderer() must have succeeded.
void configure_gles2_target(sf::RenderTarget& target);

/// Vertex stage shared by Android fragment effects and built-in shader passes.
/// It reproduces SFML's model/view/projection and texture-coordinate transforms.
const char* gles2_vertex_shader_source();

/// Converts the small GLSL 1.20 compatibility surface used by engine shaders
/// (`gl_TexCoord[0].xy` and `gl_Color`) to GLSL ES 1.00 varyings.
std::string make_gles2_fragment_shader(std::string source);

} // namespace pac::gfx
