#include "engine/pnc/room_renderer.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/pnc/avatar.hpp"
#include "engine/pnc/room.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace pac::pnc {

void RoomRenderer::draw(sf::RenderTarget& target,
                        const RoomData& room,
                        const std::string& room_dir,
                        pac::core::ResourceCache& resources,
                        const Avatar* player,
                        pac::core::Diagnostics& log) const {
    sf::RectangleShape fill(
        sf::Vector2f(static_cast<float>(room.size.x), static_cast<float>(room.size.y)));
    fill.setFillColor(room.background_color);
    target.draw(fill);

    std::vector<std::size_t> order(room.layers.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&room](std::size_t a, std::size_t b) {
        return room.layers[a].z < room.layers[b].z;
    });

    const float avatar_z = player ? player->z() : 0.0f;
    bool drew_avatar = false;
    for (const std::size_t idx : order) {
        const BackgroundLayer& layer = room.layers[idx];
        if (player && !drew_avatar && layer.z > avatar_z) {
            player->draw(target);
            drew_avatar = true;
        }
        try {
            const sf::Texture& tex =
                resources.texture(pac::core::logical_join(room_dir, layer.image));
            sf::Sprite sprite(tex);
            const sf::Vector2u ts = tex.getSize();
            if (ts.x > 0 && ts.y > 0) {
                sprite.setScale(static_cast<float>(room.size.x) / static_cast<float>(ts.x),
                                static_cast<float>(room.size.y) / static_cast<float>(ts.y));
            }
            target.draw(sprite);
        } catch (const std::exception& e) {
            log.error(e.what());
        }
    }
    if (player && !drew_avatar) {
        player->draw(target);
    }
}

} // namespace pac::pnc
