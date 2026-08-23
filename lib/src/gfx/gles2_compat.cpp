#include "gfx/gles2_compat.hpp"

#include "engine/core/diagnostics.hpp"

#include <SFML/Config.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Shader.hpp>

#include <sstream>

#if defined(SFML_SYSTEM_ANDROID)
#include <GLES2/gl2.h>
#endif

namespace pac::gfx {

namespace {

constexpr const char* kDefaultVertexShader = R"GLSL(
uniform mat4 viewMatrix;
uniform mat4 projMatrix;
uniform mat4 textMatrix;
attribute vec2 position;
attribute vec4 color;
attribute vec2 texCoord;
varying lowp vec4 vColor;
varying mediump vec2 vTexCoord;

void main() {
    gl_Position = projMatrix * viewMatrix * vec4(position, 0.0, 1.0);
    vColor = color;
    vTexCoord = (textMatrix * vec4(texCoord, 0.0, 1.0)).xy;
}
)GLSL";

constexpr const char* kDefaultFragmentShader = R"GLSL(
precision mediump float;
precision mediump int;
varying lowp vec4 vColor;
varying mediump vec2 vTexCoord;
uniform sampler2D texture;
uniform int textureEnabled;

void main() {
    vec4 texel = textureEnabled != 0 ? texture2D(texture, vTexCoord) : vec4(1.0);
    gl_FragColor = texel * vColor;
}
)GLSL";

#if defined(SFML_SYSTEM_ANDROID)
// Android owns one activity and one SFML share group for the process. Keeping
// this tiny program alive for process lifetime also avoids destruction after
// the activity's final GL context has already gone away.
sf::Shader* g_default_shader = nullptr;
#endif

void replace_all(std::string& value, const std::string& from, const std::string& to) {
    std::size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

} // namespace

const char* gles2_vertex_shader_source() {
    return kDefaultVertexShader;
}

std::string make_gles2_fragment_shader(std::string source) {
    replace_all(source, "gl_TexCoord[0].xy", "vTexCoord");
    replace_all(source, "gl_Color", "vColor");
    return std::string("precision mediump float;\n"
                       "precision mediump int;\n"
                       "varying lowp vec4 vColor;\n"
                       "varying mediump vec2 vTexCoord;\n") +
           source;
}

bool initialize_gles2_renderer(sf::RenderTarget& target, pac::core::Diagnostics& log) {
#if defined(SFML_SYSTEM_ANDROID)
    const auto gl_string = [](GLenum name) {
        const GLubyte* value = glGetString(name);
        return value ? reinterpret_cast<const char*>(value) : "unknown";
    };
    GLint texture_size = 0;
    GLint texture_units = 0;
    GLint fragment_uniforms = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_size);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texture_units);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &fragment_uniforms);
    std::ostringstream capabilities;
    capabilities << "renderer: OpenGL ES vendor='" << gl_string(GL_VENDOR)
                 << "' renderer='" << gl_string(GL_RENDERER) << "' version='"
                 << gl_string(GL_VERSION) << "' GLSL='" << gl_string(GL_SHADING_LANGUAGE_VERSION)
                 << "' max_texture=" << texture_size << " max_fragment_textures=" << texture_units
                 << " max_fragment_uniform_vectors=" << fragment_uniforms;
    log.info(capabilities.str());

    if (!sf::Shader::isAvailable()) {
        log.error("renderer: GLES2 context does not report shader support");
        return false;
    }
    if (!g_default_shader) {
        auto* shader = new sf::Shader();
        if (!shader->loadFromMemory(kDefaultVertexShader, kDefaultFragmentShader)) {
            delete shader;
            log.error("renderer: default GLES2 shader failed to compile");
            return false;
        }
        g_default_shader = shader;
    }
    target.setDefaultShader(g_default_shader);
#else
    (void) target;
    (void) log;
#endif
    return true;
}

void configure_gles2_target(sf::RenderTarget& target) {
#if defined(SFML_SYSTEM_ANDROID)
    target.setDefaultShader(g_default_shader);
#else
    (void) target;
#endif
}

} // namespace pac::gfx
