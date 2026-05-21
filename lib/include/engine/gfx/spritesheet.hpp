#pragma once

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <cstddef>
#include <map>
#include <string>

namespace sf {
class Texture;
}

namespace pac::core {
class ResourceCache;
}

namespace pac::gfx {

/// One named atlas frame: its rectangle in the atlas and frame-local anchor
/// points (origin at the frame's top-left), used as pivots/attachments.
struct Frame {
    sf::IntRect rect;
    std::map<std::string, sf::Vector2f> anchors;

    const sf::Vector2f* anchor(const std::string& name) const;
};

/// Parsed atlas metadata without a texture — headless and fully testable.
struct SpritesheetData {
    std::string image; // logical path, relative to the atlas file's directory
    sf::Vector2u size{0, 0};
    std::map<std::string, Frame> frames;

    const Frame* frame(const std::string& id) const;
    bool has(const std::string& id) const { return frame(id) != nullptr; }
    std::size_t frame_count() const { return frames.size(); }
};

/// Parse atlas YAML. Throws AssetError on malformed data.
SpritesheetData parse_spritesheet(const std::string& yaml_text);

/// Atlas metadata bound to its loaded texture (owned by the ResourceCache).
class Spritesheet {
public:
    Spritesheet(SpritesheetData data, const sf::Texture& texture);

    const sf::Texture& texture() const { return *texture_; }
    const SpritesheetData& data() const { return data_; }
    const Frame* frame(const std::string& id) const { return data_.frame(id); }

private:
    SpritesheetData data_;
    const sf::Texture* texture_;
};

/// Load atlas YAML + its texture via the cache. `logical` is the atlas file path;
/// the atlas `image` is resolved relative to the atlas file's directory.
Spritesheet load_spritesheet(pac::core::ResourceCache& res, const std::string& logical);

} // namespace pac::gfx
