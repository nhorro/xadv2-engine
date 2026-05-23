#include "engine/pnc/room_renderer.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/geom/geometry.hpp"
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/room.hpp"
#include "engine/pnc/room_runtime.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/View.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace pac::pnc {

void RoomRenderer::draw(sf::RenderTarget& target,
                        const RoomRuntime& room,
                        const std::string& room_dir,
                        pac::core::ResourceCache& resources,
                        const Avatar* player,
                        const std::vector<const Avatar*>& npcs,
                        pac::core::Diagnostics& log) const {
    const RoomData& data = room.data();

    // Solid backdrop behind every layer: fill the visible view so any world area
    // a layer doesn't cover (within or beyond the layer bounds) shows the color.
    const sf::View& view = target.getView();
    sf::RectangleShape fill(view.getSize());
    fill.setPosition(view.getCenter() - view.getSize() / 2.0f);
    fill.setFillColor(data.background_color);
    target.draw(fill);

    using DrawFn = std::function<void(sf::RenderTarget&)>;
    std::vector<std::pair<float, DrawFn>> items;

    // Background layers draw at native pixel size × the layer's uniform `scale`
    // (aspect-preserving) with the top-left at the layer's `origin` (default
    // (0,0)), so layers may differ in size and be placed freely. The room's world
    // bounds are the union of these rects.
    for (const BackgroundLayer& layer : data.layers) {
        const std::string image = pac::core::logical_join(room_dir, layer.image);
        const geom::Point origin = layer.origin;
        const float scale = layer.scale;
        items.emplace_back(layer.z, [&resources, &log, image, origin, scale](sf::RenderTarget& t) {
            try {
                sf::Sprite sprite(resources.texture(image));
                sprite.setPosition(origin.x, origin.y);
                if (scale != 1.0f) {
                    sprite.setScale(scale, scale);
                }
                t.draw(sprite);
            } catch (const std::exception& e) {
                log.error(e.what());
            }
        });
    }

    // Regions: the current state's image at the area's top-left. A region may
    // pin its depth to a named layer via `over: <layer>` (design 04 §Z-order);
    // otherwise it uses its explicit `z`.
    const auto layer_z = [&data](const std::string& layer_id) -> std::optional<float> {
        for (const BackgroundLayer& l : data.layers) {
            if (l.id == layer_id) {
                return l.z;
            }
        }
        return std::nullopt;
    };
    for (const auto& [id, region] : data.regions) {
        const auto state_it = region.states.find(room.region_state(id));
        if (state_it == region.states.end() || state_it->second.empty()) {
            continue;
        }
        float z = region.z;
        if (!region.over.empty()) {
            if (const auto lz = layer_z(region.over)) {
                z = *lz;
            } else {
                log.error("region '" + id + "': over names unknown layer '" + region.over + "'");
            }
        }
        const std::string image = pac::core::logical_join(room_dir, state_it->second);
        const sf::FloatRect bounds = geom::polygon_bounds(region.area);
        items.emplace_back(z, [&resources, &log, image, bounds](sf::RenderTarget& t) {
            try {
                sf::Sprite sprite(resources.texture(image));
                sprite.setPosition(bounds.left, bounds.top);
                t.draw(sprite);
            } catch (const std::exception& e) {
                log.error(e.what());
            }
        });
    }

    // Objects: visible ones at their position; z = explicit, or the sprite's
    // bottom edge for auto (so it sorts with avatars).
    for (const auto& [id, object] : data.objects) {
        if (!room.object_visible(id) || object.image.empty()) {
            continue;
        }
        const std::string image = pac::core::logical_join(room_dir, object.image);
        float z = object.z;
        try {
            const sf::Texture& tex = resources.texture(image);
            if (object.z_auto) {
                z = object.position.y + static_cast<float>(tex.getSize().y);
            }
        } catch (const std::exception& e) {
            log.error(e.what());
            continue;
        }
        const geom::Point pos = object.position;
        items.emplace_back(z, [&resources, &log, image, pos](sf::RenderTarget& t) {
            try {
                sf::Sprite sprite(resources.texture(image));
                sprite.setPosition(pos.x, pos.y);
                t.draw(sprite);
            } catch (const std::exception& e) {
                log.error(e.what());
            }
        });
    }

    if (player) {
        items.emplace_back(player->z(), [player](sf::RenderTarget& t) { player->draw(t); });
    }
    for (const Avatar* npc : npcs) {
        if (npc) {
            items.emplace_back(npc->z(), [npc](sf::RenderTarget& t) { npc->draw(t); });
        }
    }

    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    for (const auto& [z, draw_fn] : items) {
        draw_fn(target);
    }
}

sf::Vector2u compute_room_bounds(const RoomData& data,
                                 const std::string& room_dir,
                                 pac::core::ResourceCache& resources,
                                 sf::Vector2f viewport,
                                 pac::core::Diagnostics& log) {
    // World is anchored at (0,0); only the right/bottom extents grow it, and it is
    // never smaller than the room view. A layer placed at a negative origin spills
    // off the top-left and is never scrolled to, rather than shifting the origin.
    float right = viewport.x;
    float bottom = viewport.y;
    for (const BackgroundLayer& layer : data.layers) {
        try {
            const sf::Texture& tex =
                resources.texture(pac::core::logical_join(room_dir, layer.image));
            const sf::Vector2u ts = tex.getSize();
            right = std::max(right, layer.origin.x + static_cast<float>(ts.x) * layer.scale);
            bottom = std::max(bottom, layer.origin.y + static_cast<float>(ts.y) * layer.scale);
        } catch (const std::exception& e) {
            log.error(e.what());
        }
    }
    return {static_cast<unsigned>(std::ceil(right)), static_cast<unsigned>(std::ceil(bottom))};
}

} // namespace pac::pnc
