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

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace pac::pnc {

void RoomRenderer::draw(sf::RenderTarget& target,
                        const RoomRuntime& room,
                        const std::string& room_dir,
                        pac::core::ResourceCache& resources,
                        const Avatar* player,
                        pac::core::Diagnostics& log) const {
    const RoomData& data = room.data();

    sf::RectangleShape fill(
        sf::Vector2f(static_cast<float>(data.size.x), static_cast<float>(data.size.y)));
    fill.setFillColor(data.background_color);
    target.draw(fill);

    using DrawFn = std::function<void(sf::RenderTarget&)>;
    std::vector<std::pair<float, DrawFn>> items;

    // Background layers fill the room (scaled to room size).
    for (const BackgroundLayer& layer : data.layers) {
        const std::string image = pac::core::logical_join(room_dir, layer.image);
        items.emplace_back(layer.z, [&resources, &log, image, &data](sf::RenderTarget& t) {
            try {
                const sf::Texture& tex = resources.texture(image);
                sf::Sprite sprite(tex);
                const sf::Vector2u ts = tex.getSize();
                if (ts.x > 0 && ts.y > 0) {
                    sprite.setScale(static_cast<float>(data.size.x) / static_cast<float>(ts.x),
                                    static_cast<float>(data.size.y) / static_cast<float>(ts.y));
                }
                t.draw(sprite);
            } catch (const std::exception& e) {
                log.error(e.what());
            }
        });
    }

    // Regions: the current state's image at the area's top-left.
    for (const auto& [id, region] : data.regions) {
        const auto state_it = region.states.find(room.region_state(id));
        if (state_it == region.states.end() || state_it->second.empty()) {
            continue;
        }
        const std::string image = pac::core::logical_join(room_dir, state_it->second);
        const sf::FloatRect bounds = geom::polygon_bounds(region.area);
        items.emplace_back(region.z, [&resources, &log, image, bounds](sf::RenderTarget& t) {
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

    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    for (const auto& [z, draw_fn] : items) {
        draw_fn(target);
    }
}

} // namespace pac::pnc
