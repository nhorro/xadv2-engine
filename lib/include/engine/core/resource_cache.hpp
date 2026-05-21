#pragma once

#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <map>
#include <string>

namespace pac::core {

class ResourceSource;
class Diagnostics;

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
};

} // namespace pac::core
