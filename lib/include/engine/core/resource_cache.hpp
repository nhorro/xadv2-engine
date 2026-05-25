#pragma once

#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <map>
#include <memory>
#include <string>

namespace pac::core {

class ResourceSource;
class Diagnostics;

/// A compiled fragment shader plus which engine-reserved uniforms its source
/// actually declares. Callers set only the reserved uniforms the program uses:
/// SFML logs an error for every `setUniform` on a uniform the shader does not
/// declare (or that the GLSL compiler stripped as unused), so e.g. a static
/// colour grade with no `u_time` would otherwise spam the log each frame.
struct ShaderProgram {
    sf::Shader shader;
    bool uses_time = false;       // declares `u_time`
    bool uses_resolution = false; // declares `u_resolution`
    bool uses_texture = false;    // declares the `texture` sampler
};

/// True if GLSL `source` uses the identifier `ident` as a whole word, so the
/// reserved `texture` sampler is detected without matching the `texture2D`
/// builtin. Used to set only the reserved uniforms a shader actually declares.
bool shader_source_uses(const std::string& source, const std::string& ident);

/// Loads and caches fully-decoded assets by logical path over a ResourceSource.
/// Repeated requests for the same logical path return the same instance, so the
/// addresses are stable for the cache's lifetime (callers may hold pointers).
class ResourceCache {
public:
    ResourceCache(ResourceSource& source, Diagnostics& log);

    ResourceSource& source() const { return source_; }

    /// Throws ResourceError if the asset is missing or fails to decode.
    const sf::Texture& texture(const std::string& logical);
    const sf::SoundBuffer& sound_buffer(const std::string& logical);

    /// UI fonts are optional: returns nullptr (and logs) on failure instead of
    /// throwing, so a missing font degrades gracefully.
    const sf::Font* try_font(const std::string& logical);

    /// Loads & caches a compiled fragment shader by logical path (resources-root
    /// relative). Returns nullptr — caching the failure so it is attempted, and
    /// logged, only once — when shaders are unsupported on this GPU or the source
    /// is missing / fails to compile, so callers draw unshaded. The program is
    /// reused across draws; set uniforms per draw, gated on the returned
    /// `uses_*` flags. Render-side (needs a GL context), like texture().
    ShaderProgram* shader(const std::string& logical);

    /// Pass-throughs to the source for the loaders (parsed once, not cached here).
    std::string read_text(const std::string& logical) const;

    /// Host filesystem path, for SFML APIs that stream from a file (sf::Music) or
    /// read lazily (sf::Font). Throws if the backend has no host path.
    std::string host_path(const std::string& logical) const;

private:
    ResourceSource& source_;
    Diagnostics& log_;
    std::map<std::string, sf::Texture> textures_;
    std::map<std::string, sf::Font> fonts_;
    std::map<std::string, sf::SoundBuffer> sounds_;
    // A present key with a null pointer records a load that already failed, so a
    // missing / uncompilable shader is reported once, not every frame.
    std::map<std::string, std::unique_ptr<ShaderProgram>> shaders_;
};

} // namespace pac::core
