#pragma once

#include <string>

namespace sf {
class RenderTarget;
}

namespace pac::core {
class Diagnostics;
}

namespace pac::gfx {

// Internal renderer-profile adapter. Games select logical shader resources;
// only engine rendering code deals with GLES syntax and default programs.
bool initialize_gles2_renderer(sf::RenderTarget& target, pac::core::Diagnostics& log);
void configure_gles2_target(sf::RenderTarget& target);
const char* gles2_vertex_shader_source();
std::string make_gles2_fragment_shader(std::string source);

} // namespace pac::gfx
