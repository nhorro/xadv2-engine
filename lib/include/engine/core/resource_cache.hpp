#pragma once

#include "engine/core/resource_stats.hpp"

#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

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
    /// throwing, so a missing font degrades gracefully. The backing byte buffer
    /// is owned by the cache and lives for the cache's lifetime, so the font is
    /// safe to use after the call returns (a requirement of
    /// `sf::Font::loadFromMemory`).
    const sf::Font* try_font(const std::string& logical);

    /// Cached byte buffer for resources the engine loads via SFML's
    /// `*FromMemory` APIs and must keep alive for the lifetime of the loaded
    /// object (currently `sf::Music` — `MusicPlayer` calls this then
    /// `openFromMemory` against the returned pointer). Throws on missing or
    /// undecodable bytes. The buffer's address is stable for the cache's
    /// lifetime, so callers may hold the pointer.
    const std::vector<std::byte>* persistent_bytes(const std::string& logical);

    /// Loads & caches a compiled fragment shader by logical path (resources-root
    /// relative). Returns nullptr — caching the failure so it is attempted, and
    /// logged, only once — when shaders are unsupported on this GPU or the source
    /// is missing / fails to compile, so callers draw unshaded. The program is
    /// reused across draws; set uniforms per draw, gated on the returned
    /// `uses_*` flags. Render-side (needs a GL context), like texture().
    ShaderProgram* shader(const std::string& logical);

    /// Current cache occupancy for the profiling mode (#112): live texture / font
    /// / sound / shader counts and an upper-bound VRAM estimate. Cheap (iterates
    /// the cache maps); only sampled on the profiler's interval, never per frame.
    ResourceStats stats() const;

    /// Pass-throughs to the source for the loaders (parsed once, not cached here).
    std::string read_text(const std::string& logical) const;

    /// Host filesystem path. Available only when the backend is the filesystem
    /// source (loose-files dev path); throws `ResourceError` for a packed
    /// archive. Engine code paths that need it should fall back to
    /// `persistent_bytes` + the SFML `loadFromMemory` / `openFromMemory` APIs,
    /// which work for both backends.
    std::string host_path(const std::string& logical) const;

private:
    ResourceSource& source_;
    Diagnostics& log_;
    std::map<std::string, sf::Texture> textures_;
    std::map<std::string, sf::Font> fonts_;
    std::map<std::string, sf::SoundBuffer> sounds_;
    // Byte buffers we keep alive for SFML `*FromMemory` callers (fonts, music).
    // Backed by a unique_ptr so addresses are stable across map rehashes.
    std::map<std::string, std::unique_ptr<std::vector<std::byte>>> persistent_bytes_;
    // A present key with a null pointer records a load that already failed, so a
    // missing / uncompilable shader is reported once, not every frame.
    std::map<std::string, std::unique_ptr<ShaderProgram>> shaders_;
};

} // namespace pac::core
