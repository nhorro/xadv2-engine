#pragma once

#include <SFML/Window/Keyboard.hpp>

#include <string>
#include <vector>

namespace sf {
class RenderTarget;
class Font;
} // namespace sf

namespace pac::pnc {

class Avatar;
struct RoomData;

/// Which debug overlay layers are currently visible. Seeded from the manifest
/// `development` flags and flipped at runtime by the F1-F4 keys. Pure data + key
/// mapping, so it is headless-testable; the drawing lives in `DebugOverlay`.
struct DebugOverlayFlags {
    bool walkboxes = false; // walkable area + obstacles (F1)
    bool hotspots = false;  // hotspot areas + approach points (F2)
    bool anchors = false;   // avatar anchors + z values (F3)
    bool hud = false;       // command-builder + room/world state HUD (F4)

    [[nodiscard]] bool any() const { return walkboxes || hotspots || anchors || hud; }

    /// Flip the layer bound to `key` (F1-F4). Returns true if a layer toggled,
    /// false for any other key (so the caller can fall through to its own input).
    bool toggle(sf::Keyboard::Key key);
};

/// Renders the development overlays over a room. Pure drawing (needs a graphics
/// context); the toggle state and the HUD text are owned/assembled by the caller
/// (RoomScene), keeping this a dumb, layer-agnostic drawer.
class DebugOverlay {
public:
    /// World-space layers — call with the scenery (camera) view active: walkable
    /// (green) / obstacles (red) outlines, hotspot areas (cyan) + names + approach
    /// points (yellow), and avatar anchors (magenta) + z labels.
    void draw_world(sf::RenderTarget& target,
                    const DebugOverlayFlags& flags,
                    const RoomData& room,
                    const Avatar* player,
                    const std::vector<const Avatar*>& npcs,
                    const sf::Font* font) const;

    /// HUD panel — call with the UI (virtual-resolution) view active. `text` is
    /// pre-assembled by the caller and drawn top-left over a translucent box.
    void draw_hud(sf::RenderTarget& target, const sf::Font* font, const std::string& text) const;
};

} // namespace pac::pnc
