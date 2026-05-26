#include "engine/core/resource_cache.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_source.hpp"

namespace pac::core {

ResourceCache::ResourceCache(ResourceSource& source, Diagnostics& log)
    : source_(source), log_(log) {}

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
    std::size_t pos = source.find(ident);
    while (pos != std::string::npos) {
        const bool left_ok = (pos == 0) || !is_ident(source[pos - 1]);
        const std::size_t end = pos + ident.size();
        const bool right_ok = (end >= source.size()) || !is_ident(source[end]);
        if (left_ok && right_ok) {
            return true;
        }
        pos = source.find(ident, pos + 1);
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
    try {
        source = source_.read_text(logical);
    } catch (const std::exception& e) {
        log_.error(std::string("shader: ") + e.what());
        shaders_.emplace(logical, nullptr);
        return nullptr;
    }
    auto program = std::make_unique<ShaderProgram>();
    if (!program->shader.loadFromMemory(source, sf::Shader::Fragment)) {
        log_.error("shader: '" + logical + "' failed to compile");
        shaders_.emplace(logical, nullptr);
        return nullptr;
    }
    program->uses_time = shader_source_uses(source, "u_time");
    program->uses_resolution = shader_source_uses(source, "u_resolution");
    program->uses_texture = shader_source_uses(source, "texture");
    ShaderProgram* ptr = program.get();
    shaders_.emplace(logical, std::move(program));
    return ptr;
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
