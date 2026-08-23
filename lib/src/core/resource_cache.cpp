#include "engine/core/resource_cache.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/gfx/gles2_compat.hpp"

#include <SFML/Config.hpp>

namespace {

#if defined(SFML_SYSTEM_ANDROID)
std::string gles2_shader_variant(const std::string& logical) {
    constexpr const char* suffix = ".frag";
    if (logical.size() >= 5 && logical.compare(logical.size() - 5, 5, suffix) == 0) {
        return logical.substr(0, logical.size() - 5) + ".gles.frag";
    }
    return logical + ".gles";
}
#endif

} // namespace

namespace pac::core {

ResourceCache::ResourceCache(ResourceSource& source, Diagnostics& log, bool smooth_textures)
    : source_(source), log_(log), smooth_textures_(smooth_textures) {}

const sf::Texture& ResourceCache::texture(const std::string& logical) {
    const auto it = textures_.find(logical);
    if (it != textures_.end()) {
        return it->second;
    }
    const std::vector<std::byte> bytes = source_.read_bytes(logical);
    auto [pos, inserted] = textures_.try_emplace(logical);
    if (bytes.empty() || !pos->second.loadFromMemory(bytes.data(), bytes.size())) {
        textures_.erase(pos);
        throw ResourceError("failed to load texture '" + logical + "'");
    }
    pos->second.setSmooth(smooth_textures_);
    return pos->second;
}

const sf::SoundBuffer& ResourceCache::sound_buffer(const std::string& logical) {
    const auto it = sounds_.find(logical);
    if (it != sounds_.end()) {
        return it->second;
    }
    const std::vector<std::byte> bytes = source_.read_bytes(logical);
    auto [pos, inserted] = sounds_.try_emplace(logical);
    if (bytes.empty() || !pos->second.loadFromMemory(bytes.data(), bytes.size())) {
        sounds_.erase(pos);
        throw ResourceError("failed to load sound '" + logical + "'");
    }
    return pos->second;
}

const sf::Font* ResourceCache::try_font(const std::string& logical) {
    const auto it = fonts_.find(logical);
    if (it != fonts_.end()) {
        return &it->second;
    }
    // sf::Font samples glyph data lazily from the buffer we pass in, so the
    // buffer must outlive the font — we cache it via persistent_bytes() so its
    // address is stable for the lifetime of the cache. Same path for both the
    // loose-files and packed backends.
    const std::vector<std::byte>* bytes = nullptr;
    try {
        bytes = persistent_bytes(logical);
    } catch (const std::exception& e) {
        log_.warn(std::string("font: ") + e.what());
        return nullptr;
    }
    if (!bytes || bytes->empty()) {
        log_.warn("font: could not load '" + logical + "'");
        return nullptr;
    }
    auto [pos, inserted] = fonts_.try_emplace(logical);
    if (!pos->second.loadFromMemory(bytes->data(), bytes->size())) {
        fonts_.erase(pos);
        log_.warn("font: could not decode '" + logical + "'");
        return nullptr;
    }
    return &pos->second;
}

const std::vector<std::byte>* ResourceCache::persistent_bytes(const std::string& logical) {
    const auto it = persistent_bytes_.find(logical);
    if (it != persistent_bytes_.end()) {
        return it->second.get();
    }
    auto buf = std::make_unique<std::vector<std::byte>>(source_.read_bytes(logical));
    const std::vector<std::byte>* raw = buf.get();
    persistent_bytes_.emplace(logical, std::move(buf));
    return raw;
}

// Whole-word search for an identifier in shader source, so a reserved uniform is
// only set when the program actually declares/uses it (`texture` must not match
// the `texture2D` builtin). A char is part of an identifier if alphanumeric or _.
bool shader_source_uses(const std::string& source, const std::string& ident) {
    const auto is_ident = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    // Ignore comments before looking for identifiers. Stock shaders document
    // reserved uniforms (including ones they intentionally do not use), and a
    // comment-only match would make SFML try to bind an absent uniform every
    // frame. Preserve newlines so diagnostics still point at the right line.
    std::string code = source;
    bool line_comment = false;
    bool block_comment = false;
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (line_comment) {
            if (code[i] == '\n') {
                line_comment = false;
            } else {
                code[i] = ' ';
            }
            continue;
        }
        if (block_comment) {
            if (code[i] == '*' && i + 1 < code.size() && code[i + 1] == '/') {
                code[i] = code[i + 1] = ' ';
                ++i;
                block_comment = false;
            } else if (code[i] != '\n') {
                code[i] = ' ';
            }
            continue;
        }
        if (code[i] == '/' && i + 1 < code.size() && code[i + 1] == '/') {
            code[i] = code[i + 1] = ' ';
            ++i;
            line_comment = true;
        } else if (code[i] == '/' && i + 1 < code.size() && code[i + 1] == '*') {
            code[i] = code[i + 1] = ' ';
            ++i;
            block_comment = true;
        }
    }

    std::size_t pos = code.find(ident);
    while (pos != std::string::npos) {
        const bool left_ok = (pos == 0) || !is_ident(code[pos - 1]);
        const std::size_t end = pos + ident.size();
        const bool right_ok = (end >= code.size()) || !is_ident(code[end]);
        if (left_ok && right_ok) {
            return true;
        }
        pos = code.find(ident, pos + 1);
    }
    return false;
}

ShaderProgram* ResourceCache::shader(const std::string& logical) {
    const auto it = shaders_.find(logical);
    if (it != shaders_.end()) {
        return it->second.get(); // may be null: a previously cached failure
    }
    if (!sf::Shader::isAvailable()) {
        log_.error("shader: shaders are unavailable on this GPU; '" + logical +
                   "' will draw unshaded");
        shaders_.emplace(logical, nullptr);
        return nullptr;
    }
    std::string source;
    std::string selected_logical = logical;
    try {
#if defined(SFML_SYSTEM_ANDROID)
        const std::string variant = gles2_shader_variant(logical);
        if (source_.exists(variant)) {
            selected_logical = variant;
        }
#endif
        source = source_.read_text(selected_logical);
    } catch (const std::exception& e) {
        log_.error(std::string("shader: ") + e.what());
        shaders_.emplace(logical, nullptr);
        return nullptr;
    }
    auto program = std::make_unique<ShaderProgram>();
#if defined(SFML_SYSTEM_ANDROID)
    const std::string es_source = pac::gfx::make_gles2_fragment_shader(source);
    const bool loaded = program->shader.loadFromMemory(pac::gfx::gles2_vertex_shader_source(),
                                                        es_source);
#else
    const bool loaded = program->shader.loadFromMemory(source, sf::Shader::Fragment);
#endif
    if (!loaded) {
        log_.error("shader: '" + selected_logical + "' failed to compile");
        shaders_.emplace(logical, nullptr);
        return nullptr;
    }
#if defined(SFML_SYSTEM_ANDROID)
    if (selected_logical != logical) {
        log_.info("shader: using Android variant '" + selected_logical + "'");
    }
#endif
    program->uses_time = shader_source_uses(source, "u_time");
    program->uses_resolution = shader_source_uses(source, "u_resolution");
    program->uses_texture = shader_source_uses(source, "texture");
    ShaderProgram* ptr = program.get();
    shaders_.emplace(logical, std::move(program));
    return ptr;
}

ResourceStats ResourceCache::stats() const {
    ResourceStats s;
    s.texture_count = textures_.size();
    for (const auto& [logical, tex] : textures_) {
        const sf::Vector2u size = tex.getSize();
        s.texture_bytes += static_cast<std::size_t>(size.x) * size.y * 4;
    }
    s.font_count = fonts_.size();
    s.sound_count = sounds_.size();
    for (const auto& [logical, buf] : sounds_) {
        s.sound_bytes += static_cast<std::size_t>(buf.getSampleCount()) * sizeof(sf::Int16);
    }
    // A present key with a null program records a load that already failed; only
    // count programs that actually hold a compiled shader.
    for (const auto& [logical, program] : shaders_) {
        if (program) {
            ++s.shader_count;
        }
    }
    return s;
}

std::string ResourceCache::read_text(const std::string& logical) const {
    return source_.read_text(logical);
}

std::string ResourceCache::host_path(const std::string& logical) const {
    if (auto* fs = dynamic_cast<FilesystemResourceSource*>(&source_)) {
        return fs->host_path(logical);
    }
    throw ResourceError("resource backend has no host path for '" + logical + "'");
}

} // namespace pac::core
