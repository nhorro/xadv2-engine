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
    // sf::Font::loadFromFile reads glyphs lazily, so it needs a host path. Packed
    // archives (design-for) will instead keep the byte buffer alive and use
    // loadFromMemory.
    try {
        const std::string path = host_path(logical);
        auto [pos, inserted] = fonts_.try_emplace(logical);
        if (!pos->second.loadFromFile(path)) {
            fonts_.erase(pos);
            log_.warn("font: could not load '" + logical + "'");
            return nullptr;
        }
        return &pos->second;
    } catch (const std::exception& e) {
        log_.warn(std::string("font: ") + e.what());
        return nullptr;
    }
}

sf::Shader* ResourceCache::shader(const std::string& logical) {
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
    auto compiled = std::make_unique<sf::Shader>();
    if (!compiled->loadFromMemory(source, sf::Shader::Fragment)) {
        log_.error("shader: '" + logical + "' failed to compile");
        shaders_.emplace(logical, nullptr);
        return nullptr;
    }
    sf::Shader* ptr = compiled.get();
    shaders_.emplace(logical, std::move(compiled));
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
